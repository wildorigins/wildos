#include "client.h"
#include "clipboard.h"
#include "keymap.h"

#include <QDebug>
#include <QMetaObject>

#include <freerdp/channels/channels.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/cmdline.h>
#include <freerdp/client/disp.h>
#include <freerdp/codec/color.h>
#include <freerdp/codec/region.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/gdi/gfx.h>
#include <freerdp/graphics.h>
#include <freerdp/rail.h>
#include <freerdp/window.h>
#include <winpr/synch.h>

#include <QElapsedTimer>

#include <algorithm>
#include <cstring>
#include <vector>

namespace
{
// Where the time goes when an app opens: one line per step of the
// connection, in milliseconds since it began (launch-stats reads them).
QElapsedTimer connectClock;
bool firstOf[9] = {};

void step(int which, const char *what)
{
    if (which >= 0 && which < 9) {
        if (firstOf[which]) {
            return;
        }
        firstOf[which] = true;
    }
    qInfo("wildos-rdp: step %s at %lld ms", what, connectClock.isValid() ? qint64(connectClock.elapsed()) : -1);
}

Client *clientOf(rdpContext *context)
{
    return reinterpret_cast<WrdpContext *>(context)->client;
}

// The pointer FreeRDP keeps for each Windows cursor, with the picture made
// from it once.
struct Pointer {
    rdpPointer pointer;
    QImage *image;
};

QString utf8(const RAIL_UNICODE_STRING &s)
{
    char *text = rail_string_to_utf8_string(&s);
    const QString out = QString::fromUtf8(text ? text : "");
    free(text);
    return out;
}

QImage iconImage(const ICON_INFO *info)
{
    if (!info || info->width == 0 || info->height == 0 || info->width > 256 || info->height > 256) {
        return {};
    }
    QImage image(int(info->width), int(info->height), QImage::Format_ARGB32);
    if (!freerdp_image_copy_from_icon_data(image.bits(), PIXEL_FORMAT_BGRA32, UINT32(image.bytesPerLine()), 0, 0, UINT16(info->width),
                                           UINT16(info->height), info->bitsColor, UINT16(info->cbBitsColor), info->bitsMask,
                                           UINT16(info->cbBitsMask), info->colorTable, UINT16(info->cbColorTable), info->bpp)) {
        return {};
    }
    return image;
}

QRegion regionOf(const REGION16 *region)
{
    UINT32 n = 0;
    const RECTANGLE_16 *rects = region16_rects(region, &n);
    QRegion out;
    for (UINT32 i = 0; i < n; ++i) {
        out += QRect(rects[i].left, rects[i].top, rects[i].right - rects[i].left, rects[i].bottom - rects[i].top);
    }
    return out;
}
}

// FreeRDP's callbacks. They run on FreeRDP's threads: the connection's for
// window orders and drawing, a channel's own for channel messages.
struct ClientCallbacks {
    static BOOL clientNew(freerdp *instance, rdpContext *)
    {
        instance->PreConnect = preConnect;
        instance->PostConnect = postConnect;
        instance->PostDisconnect = postDisconnect;
        instance->AuthenticateEx = authenticate;
        instance->VerifyCertificateEx = verifyCertificate;
        instance->VerifyChangedCertificateEx = verifyChangedCertificate;
        instance->LogonErrorInfo = logonErrorInfo;
        instance->RetryDialog = retryDialog;
        instance->PresentGatewayMessage = nullptr;
        return TRUE;
    }

    static void clientFree(freerdp *, rdpContext *)
    {
    }

    static int clientStart(rdpContext *)
    {
        return 0;
    }

    static int clientStop(rdpContext *)
    {
        return 0;
    }

    static BOOL preConnect(freerdp *instance)
    {
        step(0, "connecting");
        rdpContext *context = instance->context;
        rdpSettings *settings = context->settings;
        Client *self = clientOf(context);
        if (!freerdp_settings_set_uint32(settings, FreeRDP_OsMajorType, OSMAJORTYPE_UNIX)
            || !freerdp_settings_set_uint32(settings, FreeRDP_OsMinorType, OSMINORTYPE_NATIVE_WAYLAND)) {
            return FALSE;
        }
        if (PubSub_SubscribeChannelConnected(context->pubSub, channelConnected) < 0
            || PubSub_SubscribeChannelDisconnected(context->pubSub, channelDisconnected) < 0) {
            return FALSE;
        }
        // Windows lays its apps out on a desktop the size of the screen in
        // physical pixels, at the screen's own scale, so every pixel it
        // sends is one on the screen.
        if (self->m_physical.isValid()) {
            (void)freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, UINT32(self->m_physical.width()));
            (void)freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, UINT32(self->m_physical.height()));
        }
        const UINT32 percent = UINT32(qBound(100.0, self->m_scale * 100.0 + 0.5, 500.0));
        const UINT32 device = self->m_scale >= 1.6 ? 180 : self->m_scale >= 1.2 ? 140 : 100;
        (void)freerdp_settings_set_uint32(settings, FreeRDP_DesktopScaleFactor, percent);
        (void)freerdp_settings_set_uint32(settings, FreeRDP_DeviceScaleFactor, device);
        if (freerdp_settings_get_bool(settings, FreeRDP_RemoteApplicationMode)) {
            // A screen plugged in or taken away later reaches Windows
            // through this channel, without reconnecting.
            (void)freerdp_settings_set_bool(settings, FreeRDP_SupportDisplayControl, TRUE);
            // The connection now stays open while no app is, and Windows is
            // paused then. FreeRDP gives up on a connection whose data goes
            // unanswered for 9 s, so every pause dropped it and the next app
            // waited for a reconnection. An hour covers Automatic's 15
            // minutes paused (a save to disk ends the connection first);
            // tested: a 3 minute pause, and the session was Active at once.
            (void)freerdp_settings_set_uint32(settings, FreeRDP_TcpAckTimeout, 60 * 60 * 1000);
            if (self->m_layout.size() > 1 && !applyLayout(settings, self->m_layout)) {
                return FALSE;
            }
        }
        if (freerdp_settings_get_uint32(settings, FreeRDP_KeyboardLayout) == 0) {
            if (const uint32_t layout = keymap::windowsLayout()) {
                (void)freerdp_settings_set_uint32(settings, FreeRDP_KeyboardLayout, layout);
            }
        }
        (void)freerdp_settings_set_bool(settings, FreeRDP_RemoteAppLanguageBarSupported, FALSE);
        // Which process a window belongs to (its image name), so the window an
        // app being opened shows is told apart from every other window.
        (void)freerdp_settings_set_uint32(settings, FreeRDP_RemoteAppFeatureFlags,
                                          freerdp_settings_get_uint32(settings, FreeRDP_RemoteAppFeatureFlags)
                                              | TS_RAIL_CLIENTSTATUS_GET_APPID_RESPONSE_EX_SUPPORTED);
        // Text has to stay sharp: the graphics pipeline with its exact
        // codecs, not video compression, which smears small type and costs
        // the most to decode.
        (void)freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, TRUE);
        (void)freerdp_settings_set_bool(settings, FreeRDP_GfxH264, FALSE);
        (void)freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444, FALSE);
        (void)freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444v2, FALSE);
        return TRUE;
    }

    // Each monitor in its own pixels at its own scale. FreeRDP only sends
    // the scales (the monitors' attributes) when told it has them.
    static bool applyLayout(rdpSettings *settings, const QList<Monitor> &layout)
    {
        if (!freerdp_settings_set_pointer_len(settings, FreeRDP_MonitorDefArray, nullptr, size_t(layout.size()))) {
            return false;
        }
        for (qsizetype i = 0; i < layout.size(); ++i) {
            auto *m = static_cast<rdpMonitor *>(freerdp_settings_get_pointer_array_writable(settings, FreeRDP_MonitorDefArray, size_t(i)));
            if (!m) {
                return false;
            }
            const Monitor &from = layout[i];
            *m = rdpMonitor{};
            m->x = from.rect.x();
            m->y = from.rect.y();
            m->width = from.rect.width();
            m->height = from.rect.height();
            m->is_primary = i == 0;
            m->orig_screen = UINT32(i);
            m->attributes.physicalWidth = UINT32(std::max(0, from.millimetres.width()));
            m->attributes.physicalHeight = UINT32(std::max(0, from.millimetres.height()));
            m->attributes.orientation = ORIENTATION_LANDSCAPE;
            m->attributes.desktopScaleFactor = monitors::desktopScale(from.scale);
            m->attributes.deviceScaleFactor = monitors::deviceScale(from.scale);
        }
        const QRect all = monitors::bounds(layout);
        return freerdp_settings_set_uint32(settings, FreeRDP_MonitorCount, UINT32(layout.size()))
               && freerdp_settings_set_bool(settings, FreeRDP_UseMultimon, TRUE)
               && freerdp_settings_set_bool(settings, FreeRDP_SpanMonitors, FALSE)
               && freerdp_settings_set_bool(settings, FreeRDP_HasMonitorAttributes, TRUE)
               && freerdp_settings_set_uint32(settings, FreeRDP_MonitorAttributeFlags, 0)
               && freerdp_settings_set_bool(settings, FreeRDP_SupportMonitorLayoutPdu, TRUE)
               && freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, UINT32(all.width()))
               && freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, UINT32(all.height()));
    }

    static UINT displayControlCaps(DispClientContext *disp, UINT32 monitors, UINT32, UINT32)
    {
        auto *self = static_cast<Client *>(disp->custom);
        qInfo("wildos-rdp: Windows takes monitor layouts (up to %u monitors)", unsigned(monitors));
        self->m_dispReady = true;
        Q_EMIT self->displayControlReady();
        return CHANNEL_RC_OK;
    }

    static BOOL postConnect(freerdp *instance)
    {
        step(1, "connected (signed in, session ready)");
        rdpContext *context = instance->context;
        Client *self = clientOf(context);
        if (!gdi_init(instance, PIXEL_FORMAT_BGRA32)) {
            return FALSE;
        }
        rdpPointer pointer = {};
        pointer.size = sizeof(Pointer);
        pointer.New = pointerNew;
        pointer.Free = pointerFree;
        pointer.Set = pointerSet;
        pointer.SetNull = pointerSetNull;
        pointer.SetDefault = pointerSetDefault;
        pointer.SetPosition = pointerSetPosition;
        graphics_register_pointer(context->graphics, &pointer);

        context->update->BeginPaint = beginPaint;
        context->update->EndPaint = endPaint;
        context->update->DesktopResize = desktopResize;
        context->update->PlaySound = playSound;
        Q_EMIT self->connected();
        return TRUE;
    }

    static void postDisconnect(freerdp *instance)
    {
        gdi_free(instance);
    }

    static BOOL authenticate(freerdp *instance, char **username, char **password, char **, rdp_auth_reason)
    {
        // There is nobody to ask: the account and password come with the
        // arguments, and a session without them must fail, not prompt.
        Q_UNUSED(instance);
        return username && *username && **username && password && *password && **password;
    }

    static DWORD verifyCertificate(freerdp *, const char *host, UINT16, const char *, const char *, const char *, const char *, DWORD)
    {
        // /cert:tofu accepts and remembers a certificate seen for the first
        // time before ever asking; anything that reaches here is refused.
        qWarning("wildos-rdp: refused the certificate of %s", host);
        return 0;
    }

    static DWORD verifyChangedCertificate(freerdp *, const char *host, UINT16, const char *, const char *, const char *, const char *,
                                          const char *, const char *, const char *, DWORD)
    {
        qWarning("wildos-rdp: %s presented a different certificate from the one it had; refused", host);
        return 0;
    }

    static int logonErrorInfo(freerdp *, UINT32 data, UINT32 type)
    {
        qInfo("wildos-rdp: logon %s [%s] at %lld ms", freerdp_get_logon_error_info_data(data), freerdp_get_logon_error_info_type(type),
              connectClock.isValid() ? qint64(connectClock.elapsed()) : -1);
        return 1;
    }

    static SSIZE_T retryDialog(freerdp *, const char *what, size_t current, void *)
    {
        // Windows is on this PC: a dropped connection is back within a
        // second or two, or not at all.
        qInfo("wildos-rdp: reconnecting %s, attempt %zu", what, current + 1);
        return current < 2 ? 500 : 2000;
    }

    static void channelConnected(void *ctx, const ChannelConnectedEventArgs *e)
    {
        if (qEnvironmentVariableIsSet("WILDOS_RDP_DEBUG")) {
            qInfo("channel %s connected", e->name);
        }
        auto *context = static_cast<rdpContext *>(ctx);
        Client *self = clientOf(context);
        if (strcmp(e->name, RAIL_SVC_CHANNEL_NAME) == 0) {
            step(2, "app channel open");
            auto *rail = static_cast<RailClientContext *>(e->pInterface);
            rail->custom = self;
            rail->ServerExecuteResult = execResult;
            rail->ServerHandshake = serverHandshake;
            rail->ServerHandshakeEx = serverHandshakeEx;
            rail->ServerLocalMoveSize = localMoveSize;
            rail->ServerMinMaxInfo = minMaxInfo;
            rail->ServerSystemParam = serverSystemParam;
            rail->ServerCloak = serverCloak;
            rail->ServerGetAppidResponseExtended = appIdResponse;
            rdpWindowUpdate *window = context->update->window;
            window->WindowCreate = windowOrder;
            window->WindowUpdate = windowOrder;
            window->WindowIcon = windowIcon;
            window->WindowCachedIcon = windowCachedIcon;
            window->WindowDelete = windowDelete;
            window->MonitoredDesktop = monitoredDesktop;
            window->NonMonitoredDesktop = nonMonitoredDesktop;
            std::lock_guard lock(self->m_channels);
            self->m_rail = rail;
        } else if (strcmp(e->name, CLIPRDR_SVC_CHANNEL_NAME) == 0) {
            self->m_clipboard->attach(static_cast<CliprdrClientContext *>(e->pInterface));
        } else if (strcmp(e->name, RDPGFX_DVC_CHANNEL_NAME) == 0) {
            freerdp_client_OnChannelConnectedEventHandler(ctx, e);
            auto *gfx = static_cast<RdpgfxClientContext *>(e->pInterface);
            gfx->UpdateWindowFromSurface = updateWindowFromSurface;
            if (qEnvironmentVariableIsSet("WILDOS_RDP_DEBUG")) {
                watchGfx(gfx);
            }
            std::lock_guard lock(self->m_channels);
            self->m_gfx = gfx;
        } else if (strcmp(e->name, DISP_DVC_CHANNEL_NAME) == 0) {
            freerdp_client_OnChannelConnectedEventHandler(ctx, e);
            auto *disp = static_cast<DispClientContext *>(e->pInterface);
            disp->custom = self;
            disp->DisplayControlCaps = displayControlCaps;
            std::lock_guard lock(self->m_channels);
            self->m_disp = disp;
        } else {
            freerdp_client_OnChannelConnectedEventHandler(ctx, e);
        }
    }

    static void channelDisconnected(void *ctx, const ChannelDisconnectedEventArgs *e)
    {
        auto *context = static_cast<rdpContext *>(ctx);
        Client *self = clientOf(context);
        if (strcmp(e->name, RAIL_SVC_CHANNEL_NAME) == 0) {
            std::lock_guard lock(self->m_channels);
            self->m_rail = nullptr;
        } else if (strcmp(e->name, CLIPRDR_SVC_CHANNEL_NAME) == 0) {
            self->m_clipboard->detach(static_cast<CliprdrClientContext *>(e->pInterface));
        } else if (strcmp(e->name, RDPGFX_DVC_CHANNEL_NAME) == 0) {
            {
                std::lock_guard lock(self->m_channels);
                self->m_gfx = nullptr;
            }
            freerdp_client_OnChannelDisconnectedEventHandler(ctx, e);
        } else if (strcmp(e->name, DISP_DVC_CHANNEL_NAME) == 0) {
            {
                std::lock_guard lock(self->m_channels);
                self->m_disp = nullptr;
                self->m_dispReady = false;
            }
            freerdp_client_OnChannelDisconnectedEventHandler(ctx, e);
        } else {
            freerdp_client_OnChannelDisconnectedEventHandler(ctx, e);
        }
    }

    static BOOL beginPaint(rdpContext *context)
    {
        rdpGdi *gdi = context->gdi;
        gdi->primary->hdc->hwnd->invalid->null = TRUE;
        gdi->primary->hdc->hwnd->ninvalid = 0;
        return TRUE;
    }

    static BOOL endPaint(rdpContext *context)
    {
        rdpGdi *gdi = context->gdi;
        HGDI_WND hwnd = gdi->primary->hdc->hwnd;
        if (hwnd->invalid->null || hwnd->ninvalid <= 0) {
            return TRUE;
        }
        QRegion damage;
        for (INT32 i = 0; i < hwnd->ninvalid; ++i) {
            const GDI_RGN &r = hwnd->cinvalid[i];
            damage += QRect(r.x, r.y, r.w, r.h);
        }
        clientOf(context)->queuePrimaryDamage(damage);
        return TRUE;
    }

    static BOOL desktopResize(rdpContext *context)
    {
        return gdi_resize(context->gdi, freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth),
                          freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight));
    }

    static BOOL playSound(rdpContext *, const PLAY_SOUND_UPDATE *)
    {
        return TRUE;
    }

    static UINT execResult(RailClientContext *rail, const RAIL_EXEC_RESULT_ORDER *result)
    {
        auto *self = static_cast<Client *>(rail->custom);
        step(5, "app started");
        if (result->execResult != RAIL_EXEC_S_OK) {
            qWarning("wildos-rdp: Windows could not start the app (result %u, %u)", unsigned(result->execResult), unsigned(result->rawResult));
            Q_EMIT self->execFailed(result->execResult);
            freerdp_abort_connect_context(self->m_context);
        }
        return CHANNEL_RC_OK;
    }

    static UINT localMoveSize(RailClientContext *rail, const RAIL_LOCALMOVESIZE_ORDER *order)
    {
        auto *self = static_cast<Client *>(rail->custom);
        Q_EMIT self->localMoveSize(order->windowId, order->isMoveSizeStart, order->moveSizeType, order->posX, order->posY);
        return CHANNEL_RC_OK;
    }

    static UINT minMaxInfo(RailClientContext *rail, const RAIL_MINMAXINFO_ORDER *info)
    {
        auto *self = static_cast<Client *>(rail->custom);
        Q_EMIT self->minMaxInfo(info->windowId, QSize(info->minTrackWidth, info->minTrackHeight), QSize(info->maxTrackWidth, info->maxTrackHeight));
        return CHANNEL_RC_OK;
    }

    static UINT serverSystemParam(RailClientContext *, const RAIL_SYSPARAM_ORDER *)
    {
        return CHANNEL_RC_OK;
    }

    static UINT serverCloak(RailClientContext *rail, const RAIL_CLOAK *cloak)
    {
        // Windows hides windows it is still preparing (cloaks them) rather
        // than leaving them unshown; FreeRDP tells Windows its clients follow
        // this, so a cloaked window must not be on screen here either.
        auto *self = static_cast<Client *>(rail->custom);
        Q_EMIT self->windowCloaked(cloak->windowId, cloak->cloak);
        return CHANNEL_RC_OK;
    }

    static UINT appIdResponse(RailClientContext *rail, const RAIL_GET_APPID_RESP_EX *id)
    {
        auto *self = static_cast<Client *>(rail->custom);
        const QString image = QString::fromUtf16(reinterpret_cast<const char16_t *>(id->processImageName));
        const QString appUserModel = QString::fromUtf16(reinterpret_cast<const char16_t *>(id->applicationID));
        Q_EMIT self->windowProcess(id->windowID, image, appUserModel, id->processId);
        return CHANNEL_RC_OK;
    }

    static BOOL windowOrder(rdpContext *context, const WINDOW_ORDER_INFO *info, const WINDOW_STATE_ORDER *state)
    {
        if ((info->fieldFlags & WINDOW_ORDER_STATE_NEW) && (info->fieldFlags & WINDOW_ORDER_FIELD_SHOW) && state->showState != 0
            && state->windowWidth >= 160 && state->windowHeight >= 100) {
            step(6, "first window from Windows");
        }
        RailOrder o;
        o.id = info->windowId;
        o.fields = info->fieldFlags;
        o.created = info->fieldFlags & WINDOW_ORDER_STATE_NEW;
        o.owner = state->ownerWindowId;
        o.style = state->style;
        o.exStyle = state->extendedStyle;
        o.showState = state->showState;
        if (info->fieldFlags & WINDOW_ORDER_FIELD_TITLE) {
            o.title = utf8(state->titleInfo);
        }
        o.offset = QPoint(state->windowOffsetX, state->windowOffsetY);
        o.size = QSize(int(state->windowWidth), int(state->windowHeight));
        o.clientOffset = QPoint(state->clientOffsetX, state->clientOffsetY);
        o.clientSize = QSize(int(state->clientAreaWidth), int(state->clientAreaHeight));
        o.visibleOffset = QPoint(state->visibleOffsetX, state->visibleOffsetY);
        o.resizeMargins = QMargins(int(state->resizeMarginLeft), int(state->resizeMarginTop), int(state->resizeMarginRight),
                                   int(state->resizeMarginBottom));
        if (info->fieldFlags & WINDOW_ORDER_FIELD_VISIBILITY) {
            for (UINT32 i = 0; i < state->numVisibilityRects; ++i) {
                const RECTANGLE_16 &r = state->visibilityRects[i];
                o.visibility.append(QRect(r.left, r.top, r.right - r.left, r.bottom - r.top));
            }
        }
        Q_EMIT clientOf(context)->windowChanged(o);
        return TRUE;
    }

    static BOOL windowIcon(rdpContext *context, const WINDOW_ORDER_INFO *info, const WINDOW_ICON_ORDER *order)
    {
        Client *self = clientOf(context);
        const QImage image = iconImage(order->iconInfo);
        if (image.isNull()) {
            return TRUE;
        }
        if (order->iconInfo->cacheId != 0xFF) {
            self->m_icons.insert((order->iconInfo->cacheId << 16) | order->iconInfo->cacheEntry, image);
        }
        Q_EMIT self->windowIcon(info->windowId, image, info->fieldFlags & WINDOW_ORDER_FIELD_ICON_BIG);
        return TRUE;
    }

    static BOOL windowCachedIcon(rdpContext *context, const WINDOW_ORDER_INFO *info, const WINDOW_CACHED_ICON_ORDER *order)
    {
        Client *self = clientOf(context);
        const QImage image = self->m_icons.value((order->cachedIcon.cacheId << 16) | order->cachedIcon.cacheEntry);
        if (!image.isNull()) {
            Q_EMIT self->windowIcon(info->windowId, image, info->fieldFlags & WINDOW_ORDER_FIELD_ICON_BIG);
        }
        return TRUE;
    }

    static BOOL windowDelete(rdpContext *context, const WINDOW_ORDER_INFO *info)
    {
        Q_EMIT clientOf(context)->windowDeleted(info->windowId);
        return TRUE;
    }

    // Windows is told about this client (its status and settings) and asked
    // for the connection's app.
    static bool requestApp(Client *self, RailClientContext *rail)
    {
        // After a reconnection the session still has every app open: asking
        // for the first one again would open a second copy of it. Without a
        // program FreeRDP still sends the client's status and settings, and
        // only the request itself fails.
        const bool again = self->m_appRequested.exchange(true) || self->m_prepareOnly;
        rdpSettings *settings = self->m_context->settings;
        QByteArray program;
        if (again) {
            program = freerdp_settings_get_string(settings, FreeRDP_RemoteApplicationProgram);
            freerdp_settings_set_string(settings, FreeRDP_RemoteApplicationProgram, nullptr);
        }
        const UINT rc = client_rail_server_start_cmd(rail);
        if (again) {
            freerdp_settings_set_string(settings, FreeRDP_RemoteApplicationProgram, program.isEmpty() ? nullptr : program.constData());
        }
        if (rc != CHANNEL_RC_OK && !(again && rc == ERROR_INVALID_PARAMETER)) {
            qWarning("wildos-rdp: the app could not be asked for");
            return false;
        }
        Q_EMIT self->railStarted();
        return true;
    }

    // Only to time the step: FreeRDP's own answer is sent as before.
    static UINT serverHandshake(RailClientContext *rail, const RAIL_HANDSHAKE_ORDER *)
    {
        step(7, "app channel ready");
        auto *self = static_cast<Client *>(rail->custom);
        // FreeRDP's own answer, which a handler of this message replaces.
        const RAIL_HANDSHAKE_ORDER reply = {freerdp_settings_get_uint32(self->m_context->settings, FreeRDP_ClientBuild)};
        UINT rc = rail->ClientHandshake(rail, &reply);
        if (rc != CHANNEL_RC_OK) {
            return rc;
        }
        return CHANNEL_RC_OK;
    }

    static UINT serverHandshakeEx(RailClientContext *rail, const RAIL_HANDSHAKE_EX_ORDER *)
    {
        return serverHandshake(rail, nullptr);
    }

    static BOOL monitoredDesktop(rdpContext *context, const WINDOW_ORDER_INFO *info, const MONITORED_DESKTOP_ORDER *desktop)
    {
        Client *self = clientOf(context);
        if (info->fieldFlags & WINDOW_ORDER_FIELD_DESKTOP_ARC_BEGAN) {
            step(3, "Windows sends its windows");
        }
        if (info->fieldFlags & WINDOW_ORDER_FIELD_DESKTOP_ARC_COMPLETED) {
            step(4, "app asked for");
            Q_EMIT self->desktopSynced();
            // Windows is ready for RemoteApp: now the app is asked for. (Asking
            // at the channel's handshake instead gains nothing: this comes 15
            // ms after it, measured.)
            RailClientContext *rail = nullptr;
            {
                std::lock_guard lock(self->m_channels);
                rail = self->m_rail;
            }
            if (rail && !requestApp(self, rail)) {
                return FALSE;
            }
        }
        const bool hasZ = info->fieldFlags & WINDOW_ORDER_FIELD_DESKTOP_ZORDER;
        const bool hasActive = info->fieldFlags & WINDOW_ORDER_FIELD_DESKTOP_ACTIVE_WND;
        if (hasZ || hasActive) {
            QList<quint32> z;
            if (hasZ && desktop->windowIds) {
                z.reserve(qsizetype(desktop->numWindowIds));
                for (UINT32 i = 0; i < desktop->numWindowIds; ++i) {
                    z.append(desktop->windowIds[i]);
                }
            }
            Q_EMIT self->desktopChanged(desktop->activeWindowId, hasActive, z, hasZ);
        }
        return TRUE;
    }

    static BOOL nonMonitoredDesktop(rdpContext *, const WINDOW_ORDER_INFO *)
    {
        return TRUE;
    }

    // With WILDOS_RDP_DEBUG: which surfaces Windows makes and which codecs it
    // draws them with, to see where memory and time go.
    static inline pcRdpgfxCreateSurface createSurface = nullptr;
    static inline pcRdpgfxDeleteSurface deleteSurface = nullptr;
    static inline pcRdpgfxSurfaceCommand surfaceCommand = nullptr;
    static inline QHash<quint32, quint64> codecBytes;

    static void watchGfx(RdpgfxClientContext *gfx)
    {
        createSurface = gfx->CreateSurface;
        deleteSurface = gfx->DeleteSurface;
        surfaceCommand = gfx->SurfaceCommand;
        gfx->CreateSurface = [](RdpgfxClientContext *c, const RDPGFX_CREATE_SURFACE_PDU *p) -> UINT {
            qInfo("gfx surface %u created %ux%u format %u", p->surfaceId, p->width, p->height, p->pixelFormat);
            return createSurface(c, p);
        };
        gfx->DeleteSurface = [](RdpgfxClientContext *c, const RDPGFX_DELETE_SURFACE_PDU *p) -> UINT {
            qInfo("gfx surface %u deleted", p->surfaceId);
            return deleteSurface(c, p);
        };
        gfx->SurfaceCommand = [](RdpgfxClientContext *c, const RDPGFX_SURFACE_COMMAND *cmd) -> UINT {
            quint64 &total = codecBytes[cmd->codecId];
            if (total == 0) {
                qInfo("gfx codec %u first used on surface %u", cmd->codecId, cmd->surfaceId);
            }
            total += cmd->length;
            return surfaceCommand(c, cmd);
        };
    }

    static UINT updateWindowFromSurface(RdpgfxClientContext *gfx, gdiGfxSurface *surface)
    {
        // Called with the pipeline's lock held, at the end of each frame.
        auto *gdi = static_cast<rdpGdi *>(gfx->custom);
        Client *self = clientOf(gdi->context);
        const QRegion damage = regionOf(&surface->invalidRegion);
        region16_clear(&surface->invalidRegion);
        const QSize size(int(surface->mappedWidth ? surface->mappedWidth : surface->width),
                         int(surface->mappedHeight ? surface->mappedHeight : surface->height));
        if (!damage.isEmpty()) {
            self->queueSurfaceDamage(surface->windowId, surface->surfaceId, damage, size);
        }
        return CHANNEL_RC_OK;
    }

    static BOOL pointerNew(rdpContext *context, rdpPointer *p)
    {
        auto *pointer = reinterpret_cast<Pointer *>(p);
        pointer->image = nullptr;
        if (p->width == 0 || p->height == 0 || p->width > 384 || p->height > 384) {
            return TRUE;
        }
        auto *image = new QImage(int(p->width), int(p->height), QImage::Format_ARGB32);
        if (!freerdp_image_copy_from_pointer_data(image->bits(), PIXEL_FORMAT_BGRA32, UINT32(image->bytesPerLine()), 0, 0, p->width, p->height,
                                                  p->xorMaskData, p->lengthXorMask, p->andMaskData, p->lengthAndMask, p->xorBpp,
                                                  &context->gdi->palette)) {
            delete image;
            return FALSE;
        }
        pointer->image = image;
        return TRUE;
    }

    static void pointerFree(rdpContext *, rdpPointer *p)
    {
        auto *pointer = reinterpret_cast<Pointer *>(p);
        delete pointer->image;
        pointer->image = nullptr;
    }

    static BOOL pointerSet(rdpContext *context, rdpPointer *p)
    {
        auto *pointer = reinterpret_cast<Pointer *>(p);
        if (pointer->image) {
            Q_EMIT clientOf(context)->pointerImage(pointer->image->copy(), QPoint(int(p->xPos), int(p->yPos)));
        }
        return TRUE;
    }

    static BOOL pointerSetNull(rdpContext *context)
    {
        Q_EMIT clientOf(context)->pointerHidden();
        return TRUE;
    }

    static BOOL pointerSetDefault(rdpContext *context)
    {
        Q_EMIT clientOf(context)->pointerDefault();
        return TRUE;
    }

    static BOOL pointerSetPosition(rdpContext *, UINT32, UINT32)
    {
        // Windows may not move the person's pointer.
        return TRUE;
    }
};

Client::Client(QObject *parent)
    : QObject(parent)
    , m_clipboard(new Clipboard(this))
{
    qRegisterMetaType<RailOrder>();
    RDP_CLIENT_ENTRY_POINTS entry = {};
    entry.Version = RDP_CLIENT_INTERFACE_VERSION;
    entry.Size = sizeof(RDP_CLIENT_ENTRY_POINTS_V1);
    entry.ContextSize = sizeof(WrdpContext);
    entry.ClientNew = ClientCallbacks::clientNew;
    entry.ClientFree = ClientCallbacks::clientFree;
    entry.ClientStart = ClientCallbacks::clientStart;
    entry.ClientStop = ClientCallbacks::clientStop;
    m_context = freerdp_client_context_new(&entry);
    if (m_context) {
        reinterpret_cast<WrdpContext *>(m_context)->client = this;
    }
}

Client::~Client()
{
    stop();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (m_context) {
        freerdp_client_context_free(m_context);
    }
}

rdpContext *Client::context() const
{
    return m_context;
}

bool Client::configure(const QList<QByteArray> &args, QString *error)
{
    if (!m_context) {
        *error = QStringLiteral("FreeRDP could not be set up");
        return false;
    }
    std::vector<QByteArray> storage;
    storage.reserve(size_t(args.size()) + 1);
    storage.push_back(QByteArrayLiteral("wildos-rdp"));
    for (const QByteArray &a : args) {
        storage.push_back(a);
    }
    std::vector<char *> argv;
    for (QByteArray &a : storage) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    const int status = freerdp_client_settings_parse_command_line(m_context->settings, int(argv.size() - 1), argv.data(), FALSE);
    // The password was among them; FreeRDP keeps its own copy.
    for (QByteArray &a : storage) {
        a.fill('\0');
    }
    if (status != 0) {
        *error = QStringLiteral("the connection settings could not be read (%1)").arg(status);
        return false;
    }
    if (!freerdp_settings_get_bool(m_context->settings, FreeRDP_RemoteApplicationMode)) {
        *error = QStringLiteral("no app to open (/app is missing)");
        return false;
    }
    return true;
}

void Client::setDisplay(const QSize &physical, double scale)
{
    m_physical = physical;
    m_scale = scale;
}

void Client::setLayout(const QList<Monitor> &layout)
{
    m_layout = layout;
    m_origin = layout.size() > 1 ? monitors::bounds(layout).topLeft() : QPoint();
}

bool Client::sendLayout(const QList<Monitor> &layout)
{
    if (layout.isEmpty()) {
        return false;
    }
    std::lock_guard lock(m_channels);
    if (!m_disp || !m_dispReady || !m_disp->SendMonitorLayout) {
        return false;
    }
    QList<DISPLAY_CONTROL_MONITOR_LAYOUT> out;
    for (qsizetype i = 0; i < layout.size(); ++i) {
        const Monitor &m = layout[i];
        DISPLAY_CONTROL_MONITOR_LAYOUT l = {};
        l.Flags = i == 0 ? DISPLAY_CONTROL_MONITOR_PRIMARY : 0;
        l.Left = m.rect.x();
        l.Top = m.rect.y();
        l.Width = UINT32(m.rect.width());
        l.Height = UINT32(m.rect.height());
        l.PhysicalWidth = UINT32(std::max(0, m.millimetres.width()));
        l.PhysicalHeight = UINT32(std::max(0, m.millimetres.height()));
        l.Orientation = ORIENTATION_LANDSCAPE;
        l.DesktopScaleFactor = monitors::desktopScale(m.scale);
        l.DeviceScaleFactor = monitors::deviceScale(m.scale);
        out.append(l);
    }
    if (m_disp->SendMonitorLayout(m_disp, UINT32(out.size()), out.data()) != CHANNEL_RC_OK) {
        return false;
    }
    m_origin = layout.size() > 1 ? monitors::bounds(layout).topLeft() : QPoint();
    return true;
}

QSize Client::desktopSize() const
{
    return QSize(int(freerdp_settings_get_uint32(m_context->settings, FreeRDP_DesktopWidth)),
                 int(freerdp_settings_get_uint32(m_context->settings, FreeRDP_DesktopHeight)));
}

void Client::start()
{
    if (m_running || !m_context) {
        return;
    }
    m_running = true;
    connectClock.start();
    if (freerdp_client_start(m_context) != 0) {
        m_running = false;
        Q_EMIT finished(QStringLiteral("FREERDP_ERROR_START"), false);
        return;
    }
    m_thread = std::thread([this] { run(); });
}

void Client::stop()
{
    if (m_running && m_context) {
        m_stopping = true;
        freerdp_abort_connect_context(m_context);
    }
}

void Client::run()
{
    freerdp *instance = m_context->instance;
    QString error;
    bool authFailed = false;
    if (!freerdp_connect(instance)) {
        const UINT32 code = freerdp_get_last_error(m_context);
        error = QString::fromUtf8(freerdp_get_last_error_name(code));
        authFailed = code == FREERDP_ERROR_AUTHENTICATION_FAILED || code == FREERDP_ERROR_CONNECT_LOGON_FAILURE
                     || code == FREERDP_ERROR_CONNECT_WRONG_PASSWORD || code == FREERDP_ERROR_CONNECT_ACCOUNT_RESTRICTION;
        qWarning("wildos-rdp: connection failed: %s: %s", qPrintable(error), freerdp_get_last_error_string(code));
    } else {
        while (!freerdp_shall_disconnect_context(m_context)) {
            HANDLE handles[MAXIMUM_WAIT_OBJECTS] = {};
            const DWORD count = freerdp_get_event_handles(m_context, handles, ARRAYSIZE(handles));
            if (count == 0) {
                break;
            }
            if (WaitForMultipleObjects(count, handles, FALSE, INFINITE) == WAIT_FAILED) {
                break;
            }
            if (!freerdp_check_event_handles(m_context)) {
                if (m_stopping) {
                    break;
                }
                Q_EMIT connectionLost();
                if (client_auto_reconnect(instance)) {
                    Q_EMIT connectionBack();
                    continue;
                }
                const UINT32 code = freerdp_get_last_error(m_context);
                if (code != FREERDP_ERROR_SUCCESS) {
                    error = QString::fromUtf8(freerdp_get_last_error_name(code));
                }
                break;
            }
        }
        const UINT32 info = freerdp_error_info(instance);
        if (error.isEmpty() && info != ERRINFO_SUCCESS && info != ERRINFO_LOGOFF_BY_USER && info != ERRINFO_RPC_INITIATED_DISCONNECT_BY_USER) {
            error = QString::fromUtf8(freerdp_get_error_info_name(info));
        }
        if (!error.isEmpty()) {
            qWarning("wildos-rdp: disconnected: %s", qPrintable(error));
        }
    }
    if (m_stopping) {
        // Ended here, on purpose: not a failure.
        error.clear();
        authFailed = false;
    }
    freerdp_disconnect(instance);
    (void)freerdp_client_stop(m_context);
    m_running = false;
    Q_EMIT finished(error, authFailed);
}

void Client::queueSurfaceDamage(quint64 windowId, quint16 surfaceId, const QRegion &damage, const QSize &size)
{
    std::lock_guard lock(m_damageLock);
    Damage &d = m_damage[windowId];
    if (d.surfaceId != surfaceId || d.size != size) {
        d.region = QRegion();
    }
    d.surfaceId = surfaceId;
    d.size = size;
    d.region += damage;
    if (!m_deliveryQueued) {
        m_deliveryQueued = true;
        QMetaObject::invokeMethod(this, &Client::deliverDamage, Qt::QueuedConnection);
    }
}

void Client::queuePrimaryDamage(const QRegion &damage)
{
    std::lock_guard lock(m_damageLock);
    m_primaryDamage += damage;
    if (!m_deliveryQueued) {
        m_deliveryQueued = true;
        QMetaObject::invokeMethod(this, &Client::deliverDamage, Qt::QueuedConnection);
    }
}

void Client::deliverDamage()
{
    QHash<quint64, Damage> damage;
    QRegion primary;
    {
        std::lock_guard lock(m_damageLock);
        damage.swap(m_damage);
        primary.swap(m_primaryDamage);
        m_deliveryQueued = false;
    }
    for (auto it = damage.cbegin(); it != damage.cend(); ++it) {
        Q_EMIT surfaceDamaged(it.key(), it->surfaceId, it->region, it->size);
    }
    if (!primary.isEmpty()) {
        Q_EMIT primaryDamaged(primary);
    }
}

bool Client::withSurface(quint16 surfaceId, const std::function<void(const uchar *, int, QSize)> &paint)
{
    std::lock_guard lock(m_channels);
    if (!m_gfx) {
        return false;
    }
    EnterCriticalSection(&m_gfx->mux);
    auto *surface = static_cast<gdiGfxSurface *>(m_gfx->GetSurfaceData(m_gfx, surfaceId));
    if (surface && surface->data) {
        const QSize size(int(surface->mappedWidth ? surface->mappedWidth : surface->width),
                         int(surface->mappedHeight ? surface->mappedHeight : surface->height));
        paint(surface->data, int(surface->scanline), size);
    }
    LeaveCriticalSection(&m_gfx->mux);
    return surface != nullptr;
}

bool Client::withPrimary(const std::function<void(const uchar *, int, QSize)> &paint)
{
    rdpGdi *gdi = m_context ? m_context->gdi : nullptr;
    if (!gdi || !gdi->primary_buffer) {
        return false;
    }
    rdp_update_lock(m_context->update);
    paint(gdi->primary_buffer, int(gdi->stride), QSize(gdi->width, gdi->height));
    rdp_update_unlock(m_context->update);
    return true;
}

void Client::sendMouse(quint16 flags, int x, int y)
{
    if (m_running) {
        (void)freerdp_client_send_button_event(reinterpret_cast<rdpClientContext *>(m_context), FALSE, flags, x, y);
    }
}

void Client::sendExtendedMouse(quint16 flags, int x, int y)
{
    if (m_running) {
        (void)freerdp_client_send_extended_button_event(reinterpret_cast<rdpClientContext *>(m_context), FALSE, flags, x, y);
    }
}

void Client::sendWheel(quint16 flags, int x, int y)
{
    if (m_running) {
        (void)freerdp_input_send_mouse_event(m_context->input, flags, UINT16(qBound(0, x, 65535)), UINT16(qBound(0, y, 65535)));
    }
}

void Client::sendKey(bool down, bool repeat, quint32 scancode)
{
    if (m_running) {
        (void)freerdp_input_send_keyboard_event_ex(m_context->input, down, repeat, scancode);
    }
}

void Client::sendUnicode(bool down, quint16 code)
{
    if (m_running) {
        (void)freerdp_input_send_unicode_keyboard_event(m_context->input, down ? 0 : KBD_FLAGS_RELEASE, code);
    }
}

void Client::sendPause()
{
    if (m_running) {
        (void)freerdp_input_send_keyboard_pause_event(m_context->input);
    }
}

void Client::sendFocusIn(quint16 toggles)
{
    if (m_running) {
        (void)freerdp_input_send_focus_in_event(m_context->input, toggles);
    }
}

void Client::activate(quint32 id, bool active)
{
    std::lock_guard lock(m_channels);
    if (m_rail) {
        RAIL_ACTIVATE_ORDER order = {};
        order.windowId = id;
        order.enabled = active;
        (void)m_rail->ClientActivate(m_rail, &order);
    }
}

void Client::sysCommand(quint32 id, quint16 command)
{
    std::lock_guard lock(m_channels);
    if (m_rail) {
        RAIL_SYSCOMMAND_ORDER order = {};
        order.windowId = id;
        order.command = command;
        (void)m_rail->ClientSystemCommand(m_rail, &order);
    }
}

void Client::moveWindow(quint32 id, const QRect &frame)
{
    std::lock_guard lock(m_channels);
    if (m_rail) {
        RAIL_WINDOW_MOVE_ORDER order = {};
        order.windowId = id;
        order.left = INT16(frame.left());
        order.top = INT16(frame.top());
        order.right = INT16(frame.left() + frame.width());
        order.bottom = INT16(frame.top() + frame.height());
        (void)m_rail->ClientWindowMove(m_rail, &order);
    }
}

void Client::systemMenu(quint32 id, int x, int y)
{
    std::lock_guard lock(m_channels);
    if (m_rail) {
        RAIL_SYSMENU_ORDER order = {};
        order.windowId = id;
        order.left = INT16(x);
        order.top = INT16(y);
        (void)m_rail->ClientSystemMenu(m_rail, &order);
    }
}

void Client::setWorkArea(const QRect &area)
{
    std::lock_guard lock(m_channels);
    if (m_rail && area.isValid()) {
        RAIL_SYSPARAM_ORDER param = {};
        param.params = SPI_MASK_SET_WORK_AREA;
        param.workArea.left = UINT16(area.left());
        param.workArea.top = UINT16(area.top());
        param.workArea.right = UINT16(area.left() + area.width());
        param.workArea.bottom = UINT16(area.top() + area.height());
        (void)m_rail->ClientSystemParam(m_rail, &param);
    }
}

void Client::refresh(const QRect &area)
{
    if (!m_running || !m_context->update || !m_context->update->RefreshRect) {
        return;
    }
    const QRect r = area.translated(-m_origin).intersected(QRect(QPoint(0, 0), desktopSize()));
    if (r.isEmpty()) {
        return;
    }
    RECTANGLE_16 rect = {UINT16(r.left()), UINT16(r.top()), UINT16(r.left() + r.width()), UINT16(r.top() + r.height())};
    (void)m_context->update->RefreshRect(m_context, 1, &rect);
}

void Client::requestProcess(quint32 id)
{
    std::lock_guard lock(m_channels);
    if (m_rail && m_rail->ClientGetAppIdRequest) {
        RAIL_GET_APPID_REQ_ORDER request = {};
        request.windowId = id;
        (void)m_rail->ClientGetAppIdRequest(m_rail, &request);
    }
}
