#!/usr/bin/env python3
"""Writes the Wild OS ribbons for Write, Tables and Slides.

    tools/gen-ribbons.py            regenerate lo/ui/{swriter,scalc,simpress}/notebookbar_wildos.ui
                                    and lo/popupmenu/{swriter,scalc,simpress}/wildosfile.xml
    tools/gen-ribbons.py --label-files main.xcd writer.xcd calc.xcd impress.xcd
                                    regenerate the command label overrides in
                                    lo/extension/wildos-office/registry

The .ui files are GtkBuilder files in LibreOffice's notebookbar dialect. They
are long and repetitive, so the ribbon is described here as data and the
files are generated; edit this script, not the .ui files.

Button text on a ribbon comes from each command's label in LibreOffice's
registry, never from the .ui file, so the short Wild OS names ("Picture",
"Replace", "Font colour") need registry overrides. --label-files writes them,
one file per command table, into the bundled extension: LibreOffice reads an
extension after its language packs, which would otherwise put their own
en-GB names back.
"""

import os
import sys
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))
UI = os.path.join(HERE, "..", "lo", "ui")
POPUPMENU = os.path.join(HERE, "..", "lo", "popupmenu")

FIND = "vnd.sun.star.findbar:FocusToFindbar"
# Slides' Reset: LibreOffice has no command that puts a slide's placeholders
# back where its layout has them, so the bundled extension carries one.
RESET_SLIDE = "vnd.sun.star.script:WildOSOffice.Slides.Reset?language=Basic&location=application"


# Ribbon description. A tab is (id, label, groups); a group is
# (id, label, priority, columns). A column is one of
#   ("big", [items])          large icons with the label underneath
#   ("rows", [row, row])      stacked rows, each a list of items; past three
#                             the rows go on in a column beside them
#   ("gallery", [command])    a control that draws itself, such as the style cards
# and an item is "command" followed by optional flags: "|menu" for a
# drop-down arrow, "|text" to show the label beside a small icon, "|w=N" to
# fix the width of a box such as the font name, "|gap" for 4 px of space
# before the item (the font name and size boxes are apart on the boards),
# "|check" for a toggle drawn as a checkbox with its label (View > Show),
# "|label=Text" for a button's own words where its command's label won't do,
# "|noarrow" for a command
# whose LibreOffice control adds a menu arrow the boards do not show (its
# toolbox then shows no split arrows: Paste and Table keep their main action).
# Bigger priority numbers fold away first in a narrow window, into the >>
# button at the end of the ribbon.

def big(*items):
    return ("big", list(items))


def rows(*rs):
    return ("rows", [list(r) for r in rs])


def gallery(command):
    return ("gallery", [command])


# The File button's menu: a LibreOffice popup menu, so the recent documents
# entry opens its list as it does in the menu bar.
FILE_MENU_NAME = "wildosfile"


# The File menu's wording, as on the OfficeFileMenu board. A popup menu
# shows each command's registry label, so these go into the label overrides
# with the ribbon's (see labels()).
FILE_LABELS = {
    ".uno:AddDirect": "New",
    ".uno:Open": "Open…",
    ".uno:RecentFileList": "Recent",
    ".uno:Save": "Save",
    ".uno:SaveAs": "Save as…",
    ".uno:SaveACopy": "Save a copy…",
    ".uno:ExportToPDF": "Export as PDF…",
    ".uno:ExportTo": "Export…",
    ".uno:SendMailDocAsPDF": "Email as PDF…",
    ".uno:PrintPreview": "Print preview",
    ".uno:Print": "Print…",
    ".uno:PrinterSetup": "Printer settings…",
    ".uno:SetDocumentProperties": "Properties…",
    ".uno:Signature": "Digital signatures…",
    ".uno:OptionsTreeDialog": "Options…",
    ".uno:HelpIndex": "Help",
    ".uno:CloseDoc": "Close",
}

FILE_MENU = {
    "swriter": [
        ".uno:AddDirect", ".uno:Open", ".uno:RecentFileList", "-",
        ".uno:Save", ".uno:SaveAs", ".uno:SaveACopy", "-",
        ".uno:ExportToPDF", ".uno:ExportTo", ".uno:SendMailDocAsPDF", "-",
        ".uno:PrintPreview", ".uno:Print", "-",
        ".uno:SetDocumentProperties", ".uno:Signature", "-",
        ".uno:OptionsTreeDialog", ".uno:HelpIndex", "-",
        ".uno:CloseDoc",
    ],
    "scalc": [
        ".uno:AddDirect", ".uno:Open", ".uno:RecentFileList", "-",
        ".uno:Save", ".uno:SaveAs", ".uno:SaveACopy", "-",
        ".uno:ExportToPDF", ".uno:ExportTo", ".uno:SendMailDocAsPDF", "-",
        ".uno:PrintPreview", ".uno:Print", ".uno:PrinterSetup", "-",
        ".uno:SetDocumentProperties", ".uno:Signature", "-",
        ".uno:OptionsTreeDialog", ".uno:HelpIndex", "-",
        ".uno:CloseDoc",
    ],
    "simpress": [
        ".uno:AddDirect", ".uno:Open", ".uno:RecentFileList", "-",
        ".uno:Save", ".uno:SaveAs", ".uno:SaveACopy", "-",
        ".uno:ExportToPDF", ".uno:ExportTo", ".uno:SendMailDocAsPDF", "-",
        ".uno:Print", ".uno:PrinterSetup", "-",
        ".uno:SetDocumentProperties", ".uno:Signature", "-",
        ".uno:OptionsTreeDialog", ".uno:HelpIndex", "-",
        ".uno:CloseDoc",
    ],
}

CLIPBOARD = ("Clipboard", "Clipboard", 1, [
    big(".uno:Paste|menu"),
    rows([".uno:Cut|text"], [".uno:Copy|text"], [".uno:FormatPaintbrush|text"]),
])

WRITER = [
    ("Home", "Home", [
        CLIPBOARD,
        ("Font", "Font", 1, [
            rows([".uno:CharFontName|w=150", ".uno:FontHeight|w=54|gap", ".uno:Grow", ".uno:Shrink"],
                 [".uno:Bold", ".uno:Italic", ".uno:Underline|menu", ".uno:Strikeout", ".uno:SubScript",
                  ".uno:SuperScript", ".uno:Color|menu", ".uno:CharBackColor|menu", ".uno:ResetAttributes"]),
        ]),
        ("Paragraph", "Paragraph", 2, [
            rows([".uno:DefaultBullet|menu", ".uno:DefaultNumbering|menu", ".uno:DecrementIndent",
                  ".uno:IncrementIndent", ".uno:ControlCodes"],
                 [".uno:LeftPara", ".uno:CenterPara", ".uno:RightPara", ".uno:JustifyPara",
                  ".uno:LineSpacing|menu", ".uno:SetBorderStyle|menu"]),
        ]),
        ("Styles", "Styles", 4, [
            gallery(".uno:StylesPreview"),
        ]),
        ("Editing", "Editing", 2, [
            rows([FIND + "|text"], [".uno:SearchDialog|text"], [".uno:SelectAll|text"]),
        ]),
    ]),
    ("Insert", "Insert", [
        ("Pages", "Pages", 2, [
            big(".uno:TitlePageDialog"),
            rows([".uno:InsertPagebreak|text"], [".uno:InsertColumnBreak|text"],
                 [".uno:InsertBreak|text"]),
        ]),
        ("Tables", "Tables", 1, [
            big(".uno:InsertTable|menu"),
        ]),
        ("Illustrations", "Illustrations", 1, [
            big(".uno:InsertGraphic", ".uno:BasicShapes|menu", ".uno:InsertObjectChart"),
        ]),
        ("Links", "Links", 2, [
            big(".uno:HyperlinkDialog"),
            rows([".uno:InsertBookmark|text"], [".uno:InsertReferenceField|text"]),
        ]),
        ("Comments", "Comments", 2, [
            big(".uno:InsertAnnotation"),
        ]),
        ("HeaderFooter", "Header and footer", 2, [
            rows([".uno:InsertPageHeader|text"], [".uno:InsertPageFooter|text"],
                 [".uno:PageNumberWizard|text"]),
        ]),
        ("TextObjects", "Text", 3, [
            big(".uno:Text"),
            rows([".uno:InsertFieldCtrl|menu|text"], [".uno:InsertSection|text"]),
        ]),
        ("Symbols", "Symbols", 3, [
            big(".uno:CharmapControl|menu", ".uno:InsertObjectStarMath"),
        ]),
    ]),
    ("Layout", "Layout", [
        ("PageSetup", "Page setup", 1, [
            big(".uno:PageMargin|menu", ".uno:Orientation|menu", ".uno:AttributePageSize|menu",
                ".uno:PageColumnType|menu"),
        ]),
        ("Breaks", "Breaks", 2, [
            rows([".uno:InsertPagebreak|text"], [".uno:InsertColumnBreak|text"],
                 [".uno:InsertBreak|text"]),
        ]),
        ("PageStyle", "Page", 2, [
            big(".uno:PageDialog"),
            rows([".uno:LineNumberingDialog|text"], [".uno:Hyphenate|text"], [".uno:Watermark|text"]),
        ]),
        ("Arrange", "Arrange", 3, [
            big(".uno:WrapMenu|menu"),
            rows([".uno:BringToFront|text"], [".uno:SendToBack|text"],
                 [".uno:ObjectAlign|menu|text"]),
        ]),
    ]),
    ("References", "References", [
        ("Contents", "Table of contents", 1, [
            big(".uno:IndexesMenu|menu"),
            rows([".uno:UpdateCurIndex|text"], [".uno:UpdateAllIndexes|text"]),
        ]),
        ("Footnotes", "Footnotes", 1, [
            big(".uno:InsertFootnote"),
            rows([".uno:InsertEndnote|text"], [".uno:FootnoteDialog|text"]),
        ]),
        ("Citations", "Citations", 2, [
            big(".uno:InsertAuthoritiesEntry"),
            rows([".uno:BibliographyComponent|text"]),
        ]),
        ("Captions", "Captions", 2, [
            big(".uno:InsertCaptionDialog"),
            rows([".uno:InsertReferenceField|text"], [".uno:InsertBookmark|text"]),
        ]),
        ("Index", "Index", 3, [
            big(".uno:InsertIndexesEntry"),
            rows([".uno:InsertMultiIndex|text"]),
        ]),
    ]),
    ("Review", "Review", [
        ("Proofing", "Proofing", 1, [
            big(".uno:SpellingAndGrammarDialog"),
            rows([".uno:SpellOnline|text"], [".uno:ThesaurusDialog|text"],
                 [".uno:WordCountDialog|text"]),
        ]),
        ("Language", "Language", 2, [
            big(".uno:LanguageMenu|menu"),
        ]),
        ("Comments", "Comments", 1, [
            big(".uno:InsertAnnotation"),
            rows([".uno:ShowAnnotations|text"], [".uno:DeleteAllNotes|text"]),
        ]),
        ("Tracking", "Track changes", 1, [
            big(".uno:TrackChanges"),
            rows([".uno:ShowTrackedChanges|text|noarrow"], [".uno:AcceptTrackedChanges|text"]),
        ]),
        ("Changes", "Changes", 2, [
            rows([".uno:AcceptTrackedChange|text"], [".uno:RejectTrackedChange|text"]),
            rows([".uno:PreviousTrackedChange|text"], [".uno:NextTrackedChange|text"]),
        ]),
        ("Compare", "Compare and protect", 3, [
            rows([".uno:CompareDocuments|text"], [".uno:ProtectTraceChangeMode|text"]),
        ]),
    ]),
    ("View", "View", [
        ("Views", "Views", 1, [
            big(".uno:PrintLayout", ".uno:BrowseView"),
        ]),
        ("Show", "Show", 1, [
            rows([".uno:Ruler|check"], [".uno:ControlCodes|check"], [".uno:ShowWhitespace|check"]),
            rows([".uno:Navigator|check"], [".uno:Sidebar|check"], [".uno:ShowAnnotations|check|label=Comments pane"]),
        ]),
        ("Zoom", "Zoom", 1, [
            big(".uno:Zoom", ".uno:Zoom100Percent"),
            rows([".uno:ZoomPage|text"], [".uno:ZoomPageWidth|text"]),
        ]),
        ("Window", "Window", 2, [
            big(".uno:FullScreen"),
        ]),
    ]),
]

CALC = [
    ("Home", "Home", [
        CLIPBOARD,
        ("Font", "Font", 1, [
            rows([".uno:CharFontName|w=128", ".uno:FontHeight|w=54|gap", ".uno:Grow", ".uno:Shrink"],
                 [".uno:Bold", ".uno:Italic", ".uno:Underline|menu", ".uno:SetBorderStyle|menu",
                  ".uno:BackgroundColor|menu", ".uno:Color|menu"]),
        ]),
        ("Alignment", "Alignment", 2, [
            rows([".uno:AlignTop", ".uno:AlignVCenter", ".uno:AlignBottom", ".uno:WrapText|text"],
                 [".uno:AlignLeft", ".uno:AlignHorizontalCenter", ".uno:AlignRight",
                  ".uno:DecrementIndent", ".uno:IncrementIndent"],
                 [".uno:ToggleMergeCells|menu|text"]),
        ]),
        ("Number", "Number", 2, [
            rows([".uno:NumberFormatType|w=124"],
                 [".uno:NumberFormatCurrency|menu", ".uno:NumberFormatPercent", ".uno:NumberFormatThousands",
                  ".uno:NumberFormatIncDecimals", ".uno:NumberFormatDecDecimals"]),
        ]),
        ("Styles", "Styles", 3, [
            big(".uno:ConditionalFormatMenu|menu", ".uno:AutoFormat"),
        ]),
        ("Cells", "Cells", 3, [
            big(".uno:InsertCell|menu", ".uno:DeleteCell|menu", ".uno:FormatCellDialog|menu"),
        ]),
        ("Editing", "Editing", 2, [
            rows([".uno:AutoSum|menu|text"], [".uno:FillCellsMenu|menu|text"], [".uno:Delete|menu|text"]),
            big(".uno:DataSort|menu", FIND + "|menu"),
        ]),
    ]),
    ("Insert", "Insert", [
        ("Tables", "Tables", 1, [
            big(".uno:DataDataPilotRun"),
        ]),
        ("Charts", "Charts", 1, [
            big(".uno:InsertObjectChart"),
            rows([".uno:InsertSparkline|text"]),
        ]),
        ("Illustrations", "Illustrations", 1, [
            big(".uno:InsertGraphic", ".uno:BasicShapes|menu"),
        ]),
        ("Links", "Links", 2, [
            big(".uno:HyperlinkDialog"),
        ]),
        ("Comments", "Comments", 2, [
            big(".uno:InsertAnnotation"),
        ]),
        ("TextObjects", "Text", 3, [
            big(".uno:DrawText"),
            rows([".uno:EditHeaderAndFooter|text"], [".uno:CharmapControl|menu|text"],
                 [".uno:InsertObjectStarMath|text"]),
        ]),
        ("Functions", "Functions", 2, [
            big(".uno:FunctionDialog"),
            rows([".uno:InsertName|text"], [".uno:DefineName|text"]),
        ]),
    ]),
    ("Layout", "Layout", [
        ("PageSetup", "Page setup", 1, [
            big(".uno:Margins"),
            rows([".uno:PageFormatDialog|text|label=Page settings"], [".uno:EditHeaderAndFooter|text"]),
        ]),
        ("PrintArea", "Print area", 1, [
            rows([".uno:DefinePrintArea|text"], [".uno:EditPrintArea|text"],
                 [".uno:DeletePrintArea|text"]),
        ]),
        ("Breaks", "Breaks", 2, [
            rows([".uno:InsertRowBreak|text"], [".uno:InsertColumnBreak|text"],
                 [".uno:DeleteAllBreaks|text"]),
        ]),
        ("Size", "Cell size", 2, [
            rows([".uno:RowHeight|text"], [".uno:SetOptimalRowHeight|text"]),
            rows([".uno:ColumnWidth|text"], [".uno:SetOptimalColumnWidth|text"]),
        ]),
    ]),
    ("Data", "Data", [
        ("SortFilter", "Sort and filter", 1, [
            rows([".uno:SortAscending"], [".uno:SortDescending"]),
            big(".uno:DataSort", ".uno:DataFilterAutoFilter"),
            rows([".uno:DataFilterStandardFilter|text"], [".uno:DataFilterRemoveFilter|text"]),
        ]),
        ("Tools", "Data tools", 1, [
            big(".uno:TextToColumns", ".uno:HandleDuplicateRecords", ".uno:Validation"),
        ]),
        ("Outline", "Outline", 2, [
            rows([".uno:Group|text"], [".uno:Ungroup|text"]),
            rows([".uno:ShowDetail|text"], [".uno:HideDetail|text"]),
        ]),
        ("Analysis", "Analysis", 2, [
            rows([".uno:DataSubTotals|text"], [".uno:DataConsolidate|text"]),
            rows([".uno:GoalSeekDialog|text"], [".uno:SolverDialog|text"]),
        ]),
        ("Ranges", "Ranges", 3, [
            rows([".uno:DefineDBName|text"], [".uno:DataAreaRefresh|text"]),
        ]),
    ]),
    ("Review", "Review", [
        ("Proofing", "Proofing", 1, [
            big(".uno:SpellDialog"),
            rows([".uno:SpellOnline|text"], [".uno:ThesaurusDialog|text"]),
        ]),
        ("Comments", "Comments", 1, [
            big(".uno:InsertAnnotation"),
            rows([".uno:ShowAllNotes|text"], [".uno:HideAllNotes|text"],
                 [".uno:DeleteAllNotes|text"]),
        ]),
        ("Tracking", "Track changes", 1, [
            big(".uno:TraceChangeMode", ".uno:AcceptChanges"),
        ]),
        ("Compare", "Compare", 3, [
            big(".uno:CompareDocuments"),
        ]),
        ("Protect", "Protect", 2, [
            big(".uno:Protect", ".uno:ToolProtectionDocument"),
        ]),
    ]),
    ("View", "View", [
        ("Views", "Views", 1, [
            big(".uno:NormalViewMode", ".uno:PagebreakMode"),
        ]),
        ("Show", "Show", 1, [
            rows([".uno:InputLineVisible|check"], [".uno:ViewRowColumnHeaders|check"],
                 [".uno:ToggleSheetGrid|check"]),
            rows([".uno:ViewValueHighlighting|check"], [".uno:ToggleFormula|check"],
                 [".uno:Sidebar|check"]),
        ]),
        ("Zoom", "Zoom", 1, [
            big(".uno:Zoom", ".uno:Zoom100Percent"),
            rows([".uno:ZoomPlus|text"], [".uno:ZoomMinus|text"]),
        ]),
        ("Window", "Window", 2, [
            big(".uno:FreezePanes|menu", ".uno:SplitWindow", ".uno:FullScreen"),
        ]),
    ]),
    ("Sheet", "Sheet", [
        ("Sheets", "Sheets", 1, [
            big(".uno:Insert", ".uno:Remove"),
            rows([".uno:RenameTable|text"], [".uno:Move|text"], [".uno:SetTabBgColor|menu|text"]),
            rows([".uno:Hide|text"], [".uno:Show|text"], [".uno:InsertSheetFromFile|text"]),
        ]),
        ("Rows", "Rows and columns", 1, [
            rows([".uno:InsertRowsBefore|text"], [".uno:InsertRowsAfter|text"],
                 [".uno:DeleteRows|text"]),
            rows([".uno:InsertColumnsBefore|text"], [".uno:InsertColumnsAfter|text"],
                 [".uno:DeleteColumns|text"]),
        ]),
        ("SheetCells", "Cells", 2, [
            big(".uno:InsertCell", ".uno:DeleteCell"),
            rows([".uno:FillDown|text"], [".uno:Delete|text"]),
        ]),
    ]),
]

# Short ribbon names, in sentence case and UK English. Menus keep
# LibreOffice's own wording: see labels().
# Slides. Home is board 12.18; the other seven tabs are not on a board yet and
# hold what Impress offers, grouped as Write's and Tables' are.
IMPRESS = [
    ("Home", "Home", [
        CLIPBOARD,
        ("Slides", "Slides", 1, [
            big(".uno:InsertPage|menu"),
            rows([".uno:AssignLayout|text|noarrow"], [".uno:DuplicateSlide|text"],
                 [RESET_SLIDE + "|text|label=Reset"]),
        ]),
        ("Font", "Font", 1, [
            rows([".uno:CharFontName|w=150", ".uno:FontHeight|w=54|gap", ".uno:Grow", ".uno:Shrink"],
                 [".uno:Bold", ".uno:Italic", ".uno:Underline|menu", ".uno:Strikeout", ".uno:SubScript",
                  ".uno:SuperScript", ".uno:Color|menu", ".uno:CharBackColor|menu", ".uno:SetDefault"]),
        ]),
        ("Paragraph", "Paragraph", 2, [
            rows([".uno:DefaultBullet|menu", ".uno:DefaultNumbering|menu", ".uno:OutlineLeft",
                  ".uno:OutlineRight", ".uno:CellVertCenter"],
                 [".uno:LeftPara", ".uno:CenterPara", ".uno:RightPara", ".uno:JustifyPara",
                  ".uno:LineSpacing|menu", ".uno:SetOutline|menu"]),
        ]),
        ("Insert", "Insert", 2, [
            big(".uno:InsertGraphic", ".uno:BasicShapes|menu", ".uno:Text", ".uno:InsertObjectChart",
                ".uno:InsertTable"),
        ]),
        ("SlideShow", "Slide show", 1, [
            big(".uno:Presentation", ".uno:PresentationCurrentSlide"),
        ]),
    ]),
    ("Insert", "Insert", [
        ("Slides", "Slides", 1, [
            big(".uno:InsertPage|menu"),
            rows([".uno:DuplicateSlide|text"], [".uno:ImportSlideFromFile|text"]),
        ]),
        ("Tables", "Tables", 1, [
            big(".uno:InsertTable"),
        ]),
        ("Pictures", "Pictures", 1, [
            big(".uno:InsertGraphic"),
            rows([".uno:PhotoAlbumDialog|text"], [".uno:Gallery|text"]),
        ]),
        ("Illustrations", "Illustrations", 1, [
            big(".uno:BasicShapes|menu", ".uno:InsertObjectChart"),
            rows([".uno:LineToolbox|menu|text"], [".uno:ConnectorToolbox|menu|text"],
                 [".uno:ArrowShapes|menu|text"]),
        ]),
        ("Links", "Links", 2, [
            big(".uno:HyperlinkDialog"),
        ]),
        ("Comments", "Comments", 2, [
            big(".uno:InsertAnnotation"),
        ]),
        ("TextObjects", "Text", 2, [
            big(".uno:Text"),
            rows([".uno:HeaderAndFooter|text"], [".uno:InsertSlideField|text"],
                 [".uno:FontworkGalleryFloater|text"]),
        ]),
        ("Symbols", "Symbols", 3, [
            big(".uno:CharmapControl|menu", ".uno:InsertMath"),
        ]),
        ("Media", "Media", 3, [
            big(".uno:InsertAVMedia"),
        ]),
    ]),
    ("Design", "Design", [
        ("Masters", "Slide master", 1, [
            big(".uno:MasterSlidesPanel", ".uno:PresentationLayout"),
            rows([".uno:SlideMasterPage|text"], [".uno:ThemeDialog|text"]),
        ]),
        ("SlideSetup", "Slide", 1, [
            big(".uno:SlideSetup"),
            rows([".uno:AssignLayout|text|noarrow"], [".uno:RenameSlide|text"]),
        ]),
        ("Background", "Background", 2, [
            rows([".uno:SelectBackground|text"], [".uno:SaveBackground|text"]),
            rows([".uno:DisplayMasterBackground|check"], [".uno:DisplayMasterObjects|check"]),
        ]),
    ]),
    ("Transitions", "Transitions", [
        ("Transition", "Transition to this slide", 1, [
            big(".uno:SlideChangeWindow"),
        ]),
        ("Timing", "Timing", 1, [
            big(".uno:RehearseTimings"),
        ]),
        ("Slides", "Slides", 2, [
            rows([".uno:HideSlide|text"], [".uno:ShowSlide|text"]),
        ]),
        ("Preview", "Preview", 1, [
            big(".uno:PresentationCurrentSlide"),
        ]),
    ]),
    ("Animations", "Animations", [
        ("Animation", "Animation", 1, [
            big(".uno:CustomAnimation"),
        ]),
        ("More", "More", 2, [
            rows([".uno:AnimationEffects|text"], [".uno:AnimationObjects|text"]),
        ]),
        ("Preview", "Preview", 1, [
            big(".uno:PresentationCurrentSlide"),
        ]),
    ]),
    ("SlideShow", "Slide show", [
        ("Start", "Start", 1, [
            big(".uno:Presentation", ".uno:PresentationCurrentSlide"),
            rows([".uno:CustomShowDialog|text"]),
        ]),
        ("SetUp", "Set up", 1, [
            big(".uno:PresentationDialog", ".uno:RehearseTimings"),
            rows([".uno:HideSlide|text"], [".uno:ShowSlide|text"]),
        ]),
    ]),
    ("Review", "Review", [
        ("Proofing", "Proofing", 1, [
            big(".uno:SpellDialog"),
            rows([".uno:SpellOnline|text"], [".uno:ThesaurusDialog|text"], [".uno:Hyphenation|text"]),
        ]),
        ("Language", "Language", 2, [
            big(".uno:LanguageMenu|menu"),
        ]),
        ("Comments", "Comments", 1, [
            big(".uno:InsertAnnotation"),
            rows([".uno:PreviousAnnotation|text"], [".uno:NextAnnotation|text"],
                 [".uno:DeleteAnnotation|text"]),
            rows([".uno:ShowAnnotations|text"], [".uno:DeleteAllAnnotation|text"]),
        ]),
    ]),
    ("View", "View", [
        ("Views", "Views", 1, [
            big(".uno:NormalMultiPaneGUI", ".uno:OutlineMode", ".uno:DiaMode", ".uno:NotesMode"),
        ]),
        ("MasterViews", "Master views", 2, [
            rows([".uno:SlideMasterPage|text"], [".uno:NotesMasterPage|text"], [".uno:HandoutMode|text"]),
        ]),
        ("Show", "Show", 1, [
            rows([".uno:ShowRuler|check"], [".uno:GridVisible|check"], [".uno:HelplinesVisible|check"]),
            rows([".uno:LeftPaneImpress|check"], [".uno:BottomPaneImpress|check"], [".uno:Sidebar|check"]),
        ]),
        ("Zoom", "Zoom", 1, [
            big(".uno:Zoom", ".uno:Zoom100Percent"),
            rows([".uno:ZoomPage|text"], [".uno:ZoomPageWidth|text"]),
        ]),
        ("Colour", "Colour", 3, [
            rows([".uno:OutputQualityColor|text"], [".uno:OutputQualityGrayscale|text"],
                 [".uno:OutputQualityBlackWhite|text"]),
        ]),
        ("Window", "Window", 2, [
            big(".uno:FullScreen", ".uno:NewWindow"),
        ]),
    ]),
]

LABELS = {
    ".uno:NewWindow": "New window",
    ".uno:InsertPage": "New slide",
    ".uno:AssignLayout": "Layout",
    ".uno:DuplicateSlide": "Duplicate",
    ".uno:Presentation": "Present",
    ".uno:PresentationCurrentSlide": "From here",
    ".uno:SetDefault": "Clear formatting",
    ".uno:OutlineLeft": "Decrease list level",
    ".uno:OutlineRight": "Increase list level",
    ".uno:CellVertCenter": "Centre vertically",
    ".uno:SetOutline": "List style",
    ".uno:ImportSlideFromFile": "Slides from a file",
    ".uno:PhotoAlbumDialog": "Photo album",
    ".uno:LineToolbox": "Lines",
    ".uno:ConnectorToolbox": "Connectors",
    ".uno:ArrowShapes": "Arrows",
    ".uno:HeaderAndFooter": "Header and footer",
    ".uno:InsertSlideField": "Slide number",
    ".uno:FontworkGalleryFloater": "Word art",
    ".uno:InsertAVMedia": "Audio or video",
    ".uno:MasterSlidesPanel": "Slide masters",
    ".uno:PresentationLayout": "Change master",
    ".uno:SlideMasterPage": "Slide master",
    ".uno:ThemeDialog": "Theme colours",
    ".uno:SlideSetup": "Slide size",
    ".uno:RenameSlide": "Rename slide",
    ".uno:SelectBackground": "Background picture",
    ".uno:SaveBackground": "Save background",
    ".uno:DisplayMasterBackground": "Master background",
    ".uno:DisplayMasterObjects": "Master objects",
    ".uno:SlideChangeWindow": "Transition",
    ".uno:RehearseTimings": "Rehearse timings",
    ".uno:HideSlide": "Hide slide",
    ".uno:ShowSlide": "Show slide",
    ".uno:CustomAnimation": "Animation",
    ".uno:AnimationEffects": "Interaction",
    ".uno:AnimationObjects": "Animated picture",
    ".uno:CustomShowDialog": "Custom show",
    ".uno:PresentationDialog": "Set up show",
    ".uno:SpellDialog": "Spelling",
    ".uno:Hyphenation": "Hyphenation",
    ".uno:PreviousAnnotation": "Previous",
    ".uno:NextAnnotation": "Next",
    ".uno:DeleteAnnotation": "Delete",
    ".uno:DeleteAllAnnotation": "Delete all",
    ".uno:NormalMultiPaneGUI": "Normal",
    ".uno:OutlineMode": "Outline",
    ".uno:DiaMode": "Slide sorter",
    ".uno:NotesMode": "Notes page",
    ".uno:NotesMasterPage": "Notes master",
    ".uno:HandoutMode": "Handout master",
    ".uno:ShowRuler": "Ruler",
    ".uno:HelplinesVisible": "Guides",
    ".uno:LeftPaneImpress": "Slide panel",
    ".uno:BottomPaneImpress": "Notes",
    ".uno:OutputQualityColor": "Colour",
    ".uno:OutputQualityGrayscale": "Greyscale",
    ".uno:OutputQualityBlackWhite": "Black and white",
    ".uno:Paste": "Paste",
    ".uno:Cut": "Cut",
    ".uno:Copy": "Copy",
    ".uno:Color": "Font colour",
    ".uno:CharBackColor": "Highlight colour",
    ".uno:BackgroundColor": "Fill colour",
    ".uno:Strikeout": "Strikethrough",
    ".uno:DefaultBullet": "Bullets",
    ".uno:DefaultNumbering": "Numbering",
    ".uno:IncrementIndent": "Increase indent",
    ".uno:DecrementIndent": "Decrease indent",
    ".uno:LineSpacing": "Line spacing",
    ".uno:LeftPara": "Align left",
    ".uno:CenterPara": "Centre",
    ".uno:RightPara": "Align right",
    ".uno:JustifyPara": "Justify",
    ".uno:TextBodyParaStyle": "Body text",
    ".uno:TitleParaStyle": "Title",
    ".uno:Heading1ParaStyle": "Heading 1",
    ".uno:Heading2ParaStyle": "Heading 2",
    ".uno:QuoteParaStyle": "Quote",
    ".uno:InsertGraphic": "Picture",
    ".uno:InsertTable": "Table",
    FIND: "Find",
    ".uno:SearchDialog": "Replace",
    ".uno:CommandPopup": "Find a command",
    ".uno:InsertPagebreak": "Page break",
    ".uno:InsertObjectChart": "Chart",
    ".uno:BasicShapes": "Shapes",
    ".uno:HyperlinkDialog": "Link",
    ".uno:InsertBookmark": "Bookmark",
    ".uno:InsertReferenceField": "Cross-reference",
    ".uno:InsertAnnotation": "Comment",
    ".uno:InsertPageHeader": "Header",
    ".uno:InsertPageFooter": "Footer",
    ".uno:PageNumberWizard": "Page number",
    ".uno:Text": "Text box",
    ".uno:DrawText": "Text box",
    ".uno:InsertFieldCtrl": "Field",
    ".uno:InsertSection": "Section",
    ".uno:CharmapControl": "Symbol",
    ".uno:InsertObjectStarMath": "Equation",
    ".uno:PageMargin": "Margins",
    ".uno:Orientation": "Orientation",
    ".uno:AttributePageSize": "Size",
    ".uno:PageColumnType": "Columns",
    ".uno:InsertColumnBreak": "Column break",
    ".uno:InsertBreak": "More breaks",
    ".uno:Hyphenate": "Hyphenation",
    ".uno:LineNumberingDialog": "Line numbers",
    ".uno:PageDialog": "Page setup",
    ".uno:TitlePageDialog": "Title page",
    ".uno:FormatColumns": "Column settings",
    ".uno:WrapMenu": "Wrap text",
    ".uno:BringToFront": "Bring to front",
    ".uno:SendToBack": "Send to back",
    ".uno:ObjectAlign": "Align",
    ".uno:InsertMultiIndex": "Insert index",
    ".uno:UpdateCurIndex": "Update table",
    ".uno:UpdateAllIndexes": "Update all tables",
    ".uno:InsertFootnote": "Footnote",
    ".uno:InsertEndnote": "Endnote",
    ".uno:FootnoteDialog": "Note settings",
    ".uno:InsertAuthoritiesEntry": "Citation",
    ".uno:InsertCaptionDialog": "Caption",
    ".uno:InsertIndexesEntry": "Mark entry",
    ".uno:UpdateAll": "Update all",
    ".uno:SpellingAndGrammarDialog": "Spelling",
    ".uno:SpellDialog": "Spelling",
    ".uno:SpellOnline": "Check as you type",
    ".uno:ThesaurusDialog": "Thesaurus",
    ".uno:WordCountDialog": "Word count",
    ".uno:ShowAnnotations": "Show comments",
    ".uno:DeleteAllNotes": "Delete all comments",
    ".uno:TrackChanges": "Track changes",
    ".uno:TraceChangeMode": "Track changes",
    ".uno:ShowTrackedChanges": "Show changes",
    ".uno:AcceptTrackedChanges": "Manage changes",
    ".uno:AcceptChanges": "Manage changes",
    ".uno:AcceptTrackedChange": "Accept",
    ".uno:RejectTrackedChange": "Reject",
    ".uno:PreviousTrackedChange": "Previous",
    ".uno:NextTrackedChange": "Next",
    ".uno:CompareDocuments": "Compare",
    ".uno:ProtectTraceChangeMode": "Protect changes",
    ".uno:PrintLayout": "Print layout",
    ".uno:BrowseView": "Web layout",
    ".uno:Ruler": "Ruler",
    ".uno:ControlCodes": "Formatting marks",
    ".uno:ShowWhitespace": "Whitespace",
    ".uno:Navigator": "Navigator",
    ".uno:Sidebar": "Sidebar",
    ".uno:Zoom": "Zoom",
    ".uno:Zoom100Percent": "100%",
    ".uno:ZoomPage": "One page",
    ".uno:ZoomPageWidth": "Page width",
    ".uno:ZoomPlus": "Zoom in",
    ".uno:ZoomMinus": "Zoom out",
    ".uno:FullScreen": "Full screen",
    ".uno:AlignLeft": "Align left",
    ".uno:AlignHorizontalCenter": "Centre",
    ".uno:AlignRight": "Align right",
    ".uno:WrapText": "Wrap text",
    ".uno:ToggleMergeCells": "Merge and centre",
    ".uno:NumberFormatCurrency": "Currency",
    ".uno:NumberFormatPercent": "Percent",
    ".uno:NumberFormatIncDecimals": "More decimals",
    ".uno:NumberFormatDecDecimals": "Fewer decimals",
    ".uno:InsertCell": "Insert",
    ".uno:FormatCellDialog": "Format",
    ".uno:DataSort": "Sort",
    ".uno:DataFilterAutoFilter": "Filter",
    ".uno:AutoSum": "AutoSum",
    ".uno:DataDataPilotRun": "Pivot table",
    ".uno:InsertSparkline": "Sparkline",
    ".uno:EditHeaderAndFooter": "Header and footer",
    ".uno:FunctionDialog": "Function",
    ".uno:InsertName": "Use a name",
    ".uno:DefineName": "Manage names",
    ".uno:PageFormatDialog": "Page setup",
    ".uno:Margins": "Margins",
    ".uno:DefinePrintArea": "Set print area",
    ".uno:EditPrintArea": "Edit print area",
    ".uno:DeletePrintArea": "Clear print area",
    ".uno:InsertRowBreak": "Row break",
    ".uno:RowHeight": "Row height",
    ".uno:SetOptimalRowHeight": "Fit rows",
    ".uno:ColumnWidth": "Column width",
    ".uno:SetOptimalColumnWidth": "Fit columns",
    ".uno:ConditionalFormatMenu": "Conditional",
    ".uno:AutoFormat": "As table",
    ".uno:SortAscending": "Sort A to Z",
    ".uno:SortDescending": "Sort Z to A",
    ".uno:DataFilterStandardFilter": "Advanced filter",
    ".uno:DataFilterRemoveFilter": "Clear filter",
    ".uno:TextToColumns": "Text to columns",
    ".uno:HandleDuplicateRecords": "Duplicates",
    ".uno:Validation": "Validation",
    ".uno:Group": "Group",
    ".uno:Ungroup": "Ungroup",
    ".uno:ShowDetail": "Show detail",
    ".uno:HideDetail": "Hide detail",
    ".uno:DataSubTotals": "Subtotals",
    ".uno:DataConsolidate": "Consolidate",
    ".uno:GoalSeekDialog": "Goal seek",
    ".uno:SolverDialog": "Solver",
    ".uno:DefineDBName": "Define range",
    ".uno:DataAreaRefresh": "Refresh range",
    ".uno:ShowAllNotes": "Show all comments",
    ".uno:HideAllNotes": "Hide all comments",
    ".uno:Protect": "Protect sheet",
    ".uno:ToolProtectionDocument": "Protect workbook",
    ".uno:NormalViewMode": "Normal",
    ".uno:PagebreakMode": "Page break preview",
    ".uno:InputLineVisible": "Formula bar",
    ".uno:ViewRowColumnHeaders": "Headings",
    ".uno:ToggleSheetGrid": "Gridlines",
    ".uno:ViewValueHighlighting": "Highlight values",
    ".uno:ToggleFormula": "Show formulas",
    ".uno:FreezePanes": "Freeze panes",
    ".uno:SplitWindow": "Split",
    ".uno:Insert": "Insert sheet",
    ".uno:Remove": "Delete sheet",
    ".uno:RenameTable": "Rename",
    ".uno:Move": "Move or copy",
    ".uno:SetTabBgColor": "Tab colour",
    ".uno:Hide": "Hide sheet",
    ".uno:Show": "Unhide sheet",
    ".uno:InsertSheetFromFile": "Sheet from file",
    ".uno:InsertRowsBefore": "Rows above",
    ".uno:InsertRowsAfter": "Rows below",
    ".uno:DeleteRows": "Delete rows",
    ".uno:InsertColumnsBefore": "Columns before",
    ".uno:InsertColumnsAfter": "Columns after",
    ".uno:DeleteColumns": "Delete columns",
    ".uno:DeleteCell": "Delete",
    ".uno:FillDown": "Fill down",
    ".uno:Delete": "Clear",
    ".uno:IndexesMenu": "Contents",
    ".uno:FormatPaintbrush": "Clone",
    ".uno:Grow": "Grow font",
    ".uno:Shrink": "Shrink font",
    ".uno:SubScript": "Subscript",
    ".uno:SuperScript": "Superscript",
    ".uno:ResetAttributes": "Clear formatting",
    ".uno:SetBorderStyle": "Borders",
    ".uno:SelectAll": "Select all",
    ".uno:Watermark": "Watermark",
    ".uno:BibliographyComponent": "Bibliography",
    ".uno:LanguageMenu": "Language",
    ".uno:AlignTop": "Align top",
    ".uno:AlignVCenter": "Middle",
    ".uno:AlignBottom": "Align bottom",
    ".uno:NumberFormatThousands": "Thousands separator",
    ".uno:FillCellsMenu": "Fill",
    ".uno:DeleteAllBreaks": "Remove breaks",
}


class Writer:
    def __init__(self, module):
        self.module = module
        self.out = []
        self.ids = set()

    def uid(self, base):
        base = base.replace(" ", "")
        n, i = base, 1
        while n in self.ids:
            i += 1
            n = "%s%d" % (base, i)
        self.ids.add(n)
        return n

    def w(self, depth, text):
        self.out.append("  " * depth + text)

    def props(self, d, props):
        for k, v in props:
            self.w(d, '<property name="%s">%s</property>' % (k, escape(str(v))))

    def packing(self, d, props):
        self.w(d, "<packing>")
        self.props(d + 1, props)
        self.w(d, "</packing>")

    def box_child(self, d, pos, fn, expand=False, fill=True, padding=None, end=False):
        self.w(d, "<child>")
        fn(d + 1)
        p = [("expand", expand), ("fill", fill)]
        if padding is not None:
            p.append(("padding", padding))
        if end:
            p.append(("pack_type", "end"))
        p.append(("position", pos))
        self.packing(d + 1, [(k, str(v) if not isinstance(v, bool) else ("True" if v else "False"))
                             for k, v in p])
        self.w(d, "</child>")

    def toolbox(self, d, items, prefix, style, large=False):
        tid = self.uid(prefix + "-tb")
        self.w(d, '<object class="sfxlo-NotebookbarToolBox" id="%s">' % tid)
        p = [("visible", "True"), ("can_focus", "True"), ("valign", "center")]
        if style:
            p.append(("toolbar_style", style))
        p.append(("show_arrow", "False"))
        if large:
            p.append(("icon_size", "3"))
        elif style != "icons":
            # Stacked rows are 24 px tall on the boards (the Wild OS
            # LibreOffice reads this; otherwise every row keeps room for a
            # drop-down box).
            p.append(("wildos-row-height", "24"))
        if any("noarrow" in it.split("|")[1:] for it in items):
            p.append(("wildos-no-arrows", "True"))
        # Show toggles (Ruler, Gridlines) are drawn as a checkbox and label.
        if any("check" in it.split("|")[1:] for it in items):
            p.append(("wildos-checks", "True"))
        self.props(d + 1, p)
        for it in items:
            parts = it.split("|")
            cmd = parts[0]
            cls = "GtkMenuToolButton" if "menu" in parts[1:] else "GtkToolButton"
            name = cmd.split(":")[-1].split("?")[0]
            self.w(d + 1, "<child>")
            self.w(d + 2, '<object class="%s" id="%s">' % (cls, self.uid(prefix + "-" + name)))
            props = [("visible", "True"), ("action_name", cmd)]
            props += [("label", x[6:]) for x in parts[1:] if x.startswith("label=")]
            props += [("width_request", x[2:]) for x in parts[1:] if x.startswith("w=")]
            self.props(d + 3, props)
            self.w(d + 2, "</object>")
            self.packing(d + 2, [("expand", "False"), ("homogeneous", "False")])
            self.w(d + 1, "</child>")
        self.w(d, "</object>")

    def row(self, d, row, prefix, style):
        # A row is one toolbox, unless it mixes labelled and icon-only
        # buttons (a toolbox shows every label or none: Align top, Middle
        # and Align bottom beside "Wrap text"), or a "|gap" item asks for
        # 4 px of space (a spacer item inside one toolbox upsets the widths
        # LibreOffice gives its boxes). Then each run is a toolbox of its own.
        def labelled(it):
            return any(f in it.split("|")[1:] for f in ("text", "check"))

        runs, gaps = [], []
        for it in row:
            gap = "gap" in it.split("|")[1:]
            if runs and not gap and labelled(it) == labelled(runs[-1][-1]):
                runs[-1].append(it)
            else:
                runs.append([it])
                gaps.append(gap)
        if len(runs) == 1:
            self.toolbox(d, row, prefix, "both-horiz" if labelled(row[0]) else style)
            return
        self.w(d, '<object class="GtkBox" id="%s">' % self.uid(prefix + "-split"))
        self.props(d + 1, [("visible", "True"), ("can_focus", "False"),
                           ("spacing", "4" if any(gaps[1:]) else "0")])
        for i, run in enumerate(runs):
            self.box_child(d + 1, i, lambda dd, run=run: self.toolbox(
                dd, run, prefix, "both-horiz" if labelled(run[0]) else None))
        self.w(d, "</object>")

    def column(self, d, col, prefix):
        kind, body = col
        # The band holds up to three rows, as on the boards: a fourth row
        # goes on into a column of its own beside them.
        if kind == "rows" and len(body) > 3:
            self.w(d, '<object class="GtkBox" id="%s">' % self.uid(prefix + "-cols"))
            self.props(d + 1, [("visible", "True"), ("can_focus", "False"), ("valign", "center"),
                               ("spacing", "4")])
            for i in range(0, len(body), 3):
                self.box_child(d + 1, i // 3, lambda dd, part=body[i:i + 3]:
                               self.column(dd, ("rows", part), prefix))
            self.w(d, "</object>")
            return
        if kind == "big":
            self.toolbox(d, body, prefix, "both", large=True)
            return
        if kind == "gallery":
            self.toolbox(d, body, prefix, "icons")
            return
        bid = self.uid(prefix + "-rows")
        self.w(d, '<object class="GtkBox" id="%s">' % bid)
        self.props(d + 1, [("visible", "True"), ("can_focus", "False"), ("valign", "center"),
                           ("orientation", "vertical"), ("spacing", "2")])
        for i, row in enumerate(body):
            texty = any(f in it.split("|")[1:] for it in row for f in ("text", "check"))
            self.box_child(d + 1, i, lambda dd, row=row, texty=texty:
                           self.row(dd, row, prefix, "both-horiz" if texty else None))
        self.w(d, "</object>")

    def group(self, d, tab, grp):
        gid, label, prio, cols = grp
        prefix = "%s-%s" % (tab, gid)
        self.w(d, '<object class="VclOptionalBox" id="%s">' % self.uid(prefix))
        self.props(d + 1, [("visible", "True"), ("can_focus", "False"), ("vexpand", "True")])

        # As on the boards: 4 px above and below, 10 px either side, and the
        # controls centred over a small label. The ribbon draws the hairline
        # after every group itself.
        def stack(dd):
            self.w(dd, '<object class="GtkBox" id="%s">' % self.uid(prefix + "-stack"))
            self.props(dd + 1, [("visible", "True"), ("can_focus", "False"),
                                ("margin_start", "10"), ("margin_end", "10"),
                                ("margin_top", "4"), ("margin_bottom", "4"),
                                ("orientation", "vertical")])

            def content(d3):
                self.w(d3, '<object class="GtkBox" id="%s">' % self.uid(prefix + "-content"))
                self.props(d3 + 1, [("visible", "True"), ("can_focus", "False"),
                                    ("valign", "center"), ("vexpand", "True"), ("spacing", "4")])
                for i, col in enumerate(cols):
                    self.box_child(d3 + 1, i, lambda d4, col=col: self.column(d4, col, prefix))
                self.w(d3, "</object>")
            self.box_child(dd + 1, 0, content, expand=True)

            def lab(d3):
                self.w(d3, '<object class="GtkLabel" id="%s">' % self.uid(prefix + "-label"))
                self.props(d3 + 1, [("visible", "True"), ("can_focus", "False")])
                self.w(d3 + 1, '<property name="label" translatable="yes">%s</property>' % escape(label))
                self.props(d3 + 1, [("halign", "center")])
                self.w(d3 + 1, "<attributes>")
                self.w(d3 + 2, '<attribute name="scale" value="0.85"/>')
                self.w(d3 + 1, "</attributes>")
                self.w(d3, "</object>")
            self.box_child(dd + 1, 1, lab)
            self.w(dd, "</object>")
        self.box_child(d + 1, 0, stack)
        self.w(d + 1, "<style>")
        # LibreOffice folds the lowest priority value first.
        self.w(d + 2, '<class name="priority-%d"/>' % (10 - prio))
        self.w(d + 1, "</style>")
        self.w(d, "</object>")

    def tab(self, d, pos, tab):
        tid, label, groups = tab

        def page(dd):
            self.w(dd, '<object class="GtkBox" id="%s Tab">' % tid)
            self.props(dd + 1, [("visible", "True"), ("can_focus", "False"), ("vexpand", "True")])

            def merged(d3):
                self.w(d3, '<object class="sfxlo-PriorityMergedHBox" id="%s">' % tid)
                self.props(d3 + 1, [("visible", "True"), ("can_focus", "False"),
                                    ("margin_start", "6"), ("margin_end", "6"),
                                    ("vexpand", "True")])
                for i, g in enumerate(groups):
                    self.box_child(d3 + 1, i, lambda d4, g=g: self.group(d4, tid, g))
                self.w(d3, "</object>")
            # Expanded so the merged box gets the page's whole width to
            # decide what fits; it asks for almost none itself.
            self.box_child(dd + 1, 0, merged, expand=True)
            self.w(dd, "</object>")

        self.w(d, "<child>")
        page(d + 1)
        self.packing(d + 1, [("position", str(pos))])
        self.w(d, "</child>")
        self.w(d, '<child type="tab">')
        self.w(d + 1, '<object class="GtkLabel" id="%sLabel">' % tid)
        self.props(d + 2, [("visible", "True"), ("can_focus", "False")])
        self.w(d + 2, '<property name="label" translatable="yes">%s</property>' % escape(label))
        self.w(d + 1, "</object>")
        self.packing(d + 1, [("position", str(pos)), ("tab_fill", "False")])
        self.w(d, "</child>")

    def build(self, tabs):
        self.w(0, '<?xml version="1.0" encoding="UTF-8"?>')
        self.w(0, "<!-- Generated by packages/wildos-office/tools/gen-ribbons.py; edit that, not this. -->")
        self.w(0, '<interface domain="%s">' % {"swriter": "sw", "scalc": "sc", "simpress": "sd"}[self.module])
        self.w(1, '<requires lib="gtk+" version="3.20"/>')
        self.w(1, '<requires lib="LibreOffice" version="1.0"/>')
        self.w(1, '<object class="GtkGrid" id="NotebookBar">')
        self.props(2, [("visible", "True"), ("can_focus", "False")])
        self.w(2, "<child>")
        # NotebookBar sizes the root's first child to the window, and the
        # tab control lays itself out from its own allocation, so it sits in
        # a box rather than straight in the grid.
        self.w(3, '<object class="GtkBox" id="box">')
        self.props(4, [("visible", "True"), ("can_focus", "False"), ("hexpand", "True")])

        def tab_control(d):
            self.w(d, '<object class="sfxlo-NotebookbarTabControl" id="Tabs">')
            # File before the tabs and the search field after them are drawn
            # by the Wild OS LibreOffice (build/patches/libreoffice), which
            # reads these properties.
            # A tab control asks for the width of its widest page, which
            # would hold the window wider than a small screen; with a fixed
            # request it can shrink, and each page folds groups to fit.
            self.props(d + 1, [("visible", "True"), ("can_focus", "True"), ("hexpand", "True"),
                               ("width_request", "400")])
            self.w(d + 1, '<property name="wildos-file-label" translatable="yes">File</property>')
            self.props(d + 1, [("wildos-file-menu", FILE_MENU_NAME)])
            self.w(d + 1, '<property name="wildos-search-label" translatable="yes">Find a command</property>')
            for i, t in enumerate(tabs):
                self.tab(d + 1, i, t)
            self.w(d, "</object>")
        self.box_child(4, 0, tab_control, expand=True)
        self.w(3, "</object>")
        self.packing(3, [("left_attach", "0"), ("top_attach", "0")])
        self.w(2, "</child>")
        self.w(1, "</object>")
        self.w(0, "</interface>")
        return "\n".join(self.out) + "\n"


def commands(tabs):
    seen = []
    for _, _, groups in tabs:
        for _, _, _, cols in groups:
            for kind, body in cols:
                for it in (body if kind != "rows" else [i for r in body for i in r]):
                    c = it.split("|")[0]
                    if c not in seen:
                        seen.append(c)
    return seen


def labels():
    """Registry overrides that give ribbon buttons their Wild OS names.

    A toolbar shows a command's Label; menus and tooltips prefer its
    ContextLabel. So the short name goes in Label and LibreOffice's original
    Label moves to ContextLabel where the command has none, which keeps
    menus as they were. Run against LibreOffice's own registry to get the
    originals: tools/gen-ribbons.py --label-files main.xcd writer.xcd calc.xcd impress.xcd
    """
    from xml.etree import ElementTree as ET
    O = "{http://openoffice.org/2001/registry}"
    comps = {"GenericCommands": {}, "WriterCommands": {}, "CalcCommands": {}, "DrawImpressCommands": {}}
    for path in sys.argv[2:]:
        for cd in ET.parse(path).getroot():
            comp = comps.get(cd.get(O + "name"))
            if comp is None:
                continue
            # Most commands sit in the Commands set; the paragraph style
            # shortcuts sit in Popups.
            for st in cd.iter("node"):
                if st.get(O + "name") not in ("Commands", "Popups"):
                    continue
                for n in st.findall("node"):
                    d = comp.setdefault(n.get(O + "name"), {"sets": []})
                    d["sets"].append(st.get(O + "name"))
                    for p in n.findall("prop"):
                        v = p.find("value")
                        if st.get(O + "name") == "Commands" or p.get(O + "name") not in d:
                            d[p.get(O + "name")] = v.text if v is not None else None
    used = (set(commands(WRITER)) | set(commands(CALC)) | set(commands(IMPRESS)) | {".uno:CommandPopup"}
            | {c for m in FILE_MENU.values() for c in m if c != "-"})
    # LibreOffice gives a button an icon only if its command is marked as
    # having one (bit 1 of Properties). Some commands the ribbons use are
    # not, having no icon in LibreOffice's own themes; the ones the Wild OS
    # theme draws (lo/icons/icons.txt) are marked here.
    drawn = set()
    with open(os.path.join(HERE, "..", "lo", "icons", "icons.txt"), encoding="utf-8") as f:
        for line in f:
            if line.strip() and not line.startswith("#"):
                drawn.add(".uno:" + line.split()[0])
    out = []
    for comp, table in comps.items():
        # A command with a TargetURL may show its target's label, so the
        # target gets the same name.
        wanted = {}
        for cmd in sorted(used):
            label = LABELS.get(cmd, FILE_LABELS.get(cmd))
            if label and cmd in table:
                wanted[cmd] = label
                target = table[cmd].get("TargetURL")
                if target in table:
                    wanted[target] = label
        sets = {}
        for cmd in sorted((used & drawn & set(table)) - set(wanted)):
            if not int(table[cmd].get("Properties") or 0) & 1:
                for st in table[cmd]["sets"]:
                    sets.setdefault(st, []).extend([
                        '        <node oor:name="%s">' % escape(cmd, {'"': "&quot;"}),
                        '          <prop oor:name="Properties"><value>%d</value></prop>'
                        % (int(table[cmd].get("Properties") or 0) | 1),
                        "        </node>"])
        for cmd, label in sorted(wanted.items()):
            orig = table[cmd]
            # The File menu is ours, so its items show the Wild OS name too:
            # a menu reads PopupLabel, then ContextLabel, before Label.
            in_menu = cmd in FILE_LABELS
            menu_props = [k for k in ("PopupLabel", "ContextLabel")
                          if in_menu and (orig.get(k) or "").replace("~", "") != label]
            if (orig.get("Label") or "").replace("~", "") == label and not menu_props:
                continue
            value = '<value xml:lang="en-US">%s</value><value xml:lang="en-GB">%s</value>' % ((escape(label),) * 2)
            for st in orig["sets"]:
                nodes = sets.setdefault(st, [])
                nodes.append('        <node oor:name="%s">' % escape(cmd, {'"': "&quot;"}))
                nodes.append('          <prop oor:name="Label">%s</prop>' % value)
                for k in menu_props:
                    nodes.append('          <prop oor:name="%s">%s</prop>' % (k, value))
                if not in_menu and not orig.get("ContextLabel") and orig.get("Label"):
                    nodes.append('          <prop oor:name="ContextLabel"><value xml:lang="en-US">%s</value></prop>'
                                 % escape(orig["Label"]))
                if cmd in drawn and not int(orig.get("Properties") or 0) & 1:
                    nodes.append('          <prop oor:name="Properties"><value>%d</value></prop>'
                                 % (int(orig.get("Properties") or 0) | 1))
                nodes.append("        </node>")
        if sets:
            out.append('  <oor:component-data oor:package="org.openoffice.Office.UI" oor:name="%s">' % comp)
            out.append('    <node oor:name="UserInterface">')
            for name, nodes in sorted(sets.items()):
                out.append('      <node oor:name="%s">' % name)
                out.extend(nodes)
                out.append("      </node>")
            out.append("    </node>")
            out.append("  </oor:component-data>")
    return "\n".join(out) + "\n"


def file_menu(module):
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           "<!-- Generated by packages/wildos-office/tools/gen-ribbons.py; edit that, not this. -->",
           '<menu:menupopup xmlns:menu="http://openoffice.org/2001/menu">']
    for cmd in FILE_MENU[module]:
        out.append("  <menu:menuseparator/>" if cmd == "-" else
                   '  <menu:menuitem menu:id="%s"/>' % escape(cmd, {'"': "&quot;"}))
    out.append("</menu:menupopup>")
    return "\n".join(out) + "\n"


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--label-files":
        import re
        folder = os.path.join(HERE, "..", "lo", "extension", "wildos-office", "registry")
        os.makedirs(folder, exist_ok=True)
        block = re.compile(r'  <oor:component-data oor:package="org.openoffice.Office.UI" oor:name="(\w+)">\n'
                           r'(.*?)  </oor:component-data>\n', re.S)
        for name, body in block.findall(labels()):
            body = "\n".join(line[2:] if line.startswith("  ") else line for line in body.split("\n"))
            path = os.path.join(folder, name + ".xcu")
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write('<?xml version="1.0" encoding="UTF-8"?>\n'
                        "<!-- Generated by packages/wildos-office/tools/gen-ribbons.py (label files mode); "
                        "edit that, not this. -->\n"
                        '<oor:component-data xmlns:oor="http://openoffice.org/2001/registry" '
                        'xmlns:xs="http://www.w3.org/2001/XMLSchema" '
                        'oor:package="org.openoffice.Office.UI" oor:name="%s">\n%s</oor:component-data>\n'
                        % (name, body))
            print(path)
        return
    for module, tabs in (("swriter", WRITER), ("scalc", CALC), ("simpress", IMPRESS)):
        for path, text in ((os.path.join(UI, module, "notebookbar_wildos.ui"), Writer(module).build(tabs)),
                           (os.path.join(POPUPMENU, module, FILE_MENU_NAME + ".xml"), file_menu(module))):
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            print(path)


if __name__ == "__main__":
    main()
