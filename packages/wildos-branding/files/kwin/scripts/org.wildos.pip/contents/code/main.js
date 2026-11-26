// A browser's picture-in-picture video stays above other windows, as on
// Windows. Under X11 Chromium-based browsers ask for that themselves, but
// as Wayland apps they can't, and Firefox never does, so the video went
// behind the next window clicked. Their picture-in-picture windows are
// known by title, in the languages the browsers use; a title can arrive
// after the window opens.
const titles = /^(picture[ -]in[ -]picture|bild im bild|image dans l.image|imagen en imagen|imagem em imagem|immagine nell.immagine|beeld[- ]in[- ]beeld|obraz w obrazie|obraz v obraze|kép a képben|картинка в картинке|зображення в зображенні|ピクチャー ?イン ?ピクチャー|画中画|子母畫面|화면 속 화면|hình trong hình|resim içinde resim|bild[- ]i[- ]bild|kuva kuvassa|billede i billede)$/i;

function check(window) {
    if (!window || window.deleted || window.specialWindow || window.keepAbove) {
        return;
    }
    if (titles.test(String(window.caption || "").trim())) {
        window.keepAbove = true;
    }
}

function watch(window) {
    check(window);
    window.captionChanged.connect(() => check(window));
}

workspace.windowList().forEach(watch);
workspace.windowAdded.connect(watch);
