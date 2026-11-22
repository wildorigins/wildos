/*
 * Copyright 2013 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// Changed for Wild OS: the key reads the language it types in (EN, AR), as
// the Windows touch keyboard's does, instead of a globe or a smiley. A tap
// moves to the next language, or opens the language menu (with Emoji and
// Settings, where languages are chosen) when there is only one. Holding it
// opens the menu either way. Installed over Maliit's own by a dpkg
// diversion (wildos-settings' preinst), so a Maliit update keeps it.

import QtQuick 2.4

import MaliitKeyboard 2.0

ActionKey {
    readonly property bool altLangs: Keyboard.enabledLanguages.length > 1
    readonly property string code: Keyboard.activeLanguage.split(/[-@]/)[0].toUpperCase()

    label: code
    shifted: label
    noMagnifier: true

    property bool held: false;

    padding: 0
    width: panel.keyWidth

    overridePressArea: true

    action: "language"

    onPressed: {
        Feedback.keyPressed();
        held = false;
    }

    onReleased: {
        if (held) {
            return;
        }
        if (altLangs) {
            Keyboard.selectNextLanguage();
        } else {
            canvas.languageMenu.open();
        }
    }

    onPressAndHold: {
        Feedback.keyPressed();
        canvas.languageMenu.open()
        held = true;
    }
}
