#include "textbox.h"
#include "ugui/textedit.h"
#include "ugui/colorwidgets.h"
#include "touchwidgets.h"
#include "scribbleapp.h"
#include "usvg/svgnode.h"
#include "element.h"  // for setSvgFillColor


// Font families offered in the picker.  Index 0 is the bundled UI font, which is always available.  The
// remaining entries are common family names; whether they render depends on the OS providing the font (the
// renderer falls back to the bundled font + global fallback when a family isn't available).  Bold/italic
// only render if the corresponding face is loaded (see ScribbleApp font loading).
std::vector<std::string> TextBoxDialog::fontFamilies()
{
  return {"sans-serif", "serif", "monospace"};
}

static const real DEFAULT_FONT_SIZE = 24;

TextBoxDialog::TextBoxDialog(const SvgText* srcNode) : Dialog(createDialogNode())
{
  textEdit = createMultilineTextEdit(360, 160);
  textEdit->setEmptyText(_("Enter text (Enter for new line)"));

  comboFont = createComboBox(fontFamilies());
  spinSize = createTextSpinBox(DEFAULT_FONT_SIZE, 2, 4, 500);
  colorPicker = createColorEditBox(false);
  cbBold = createCheckBox();
  cbItalic = createCheckBox();

  // populate from existing node when editing
  Color color = Color::BLACK;
  if(srcNode) {
    textEdit->setText(srcNode->text().c_str());

    const char* fam = srcNode->getStringAttr("font-family");
    if(fam) {
      auto fams = fontFamilies();
      for(size_t ii = 0; ii < fams.size(); ++ii) {
        if(fams[ii] == fam) { comboFont->setIndex(int(ii)); break; }
      }
    }
    real sz = srcNode->getFloatAttr("font-size", DEFAULT_FONT_SIZE);
    if(sz > 0)
      spinSize->setValue(sz);

    // font-weight and font-style are parsed into typed attributes when a document is loaded
    //  (bold -> 700, italic -> Painter::StyleItalic), so the string form only exists before the
    //  first save -- check both
    const char* fw = srcNode->getStringAttr("font-weight");
    if((fw && strcmp(fw, "bold") == 0) || srcNode->getIntAttr("font-weight", 400) >= 550)
      cbBold->setChecked(true);
    const char* fs = srcNode->getStringAttr("font-style");
    if((fs && strcmp(fs, "italic") == 0)
        || srcNode->getIntAttr("font-style", Painter::StyleNormal) == Painter::StyleItalic)
      cbItalic->setChecked(true);

    Color c = srcNode->getColorAttr("fill", Color::BLACK);
    if(c.isValid())
      color = c;
  }

  Widget* dialogBody = selectFirst(".body-container");
  dialogBody->setMargins(0, 8);
  // large multi-line text field spanning the dialog width
  textEdit->node->setAttribute("box-anchor", "hfill");
  dialogBody->addWidget(textEdit);
  // compact style controls on a single row; each label is placed directly beside its control
  // (a plain createRow keeps label + control adjacent, unlike createTitledRow which pushes them apart)
  auto labeled = [](const char* title, Widget* control) {
    Widget* row = createRow();
    SvgText* titlenode = createTextNode(title);
    titlenode->setAttribute("margin", "0 6 0 0");
    titlenode->addClass("row-text");
    row->containerNode()->addChild(titlenode);
    row->addWidget(control);
    return row;
  };
  // Two rows so the dialog fits phone-width windows (~450 units; one row of all five controls doesn't).
  //  Each row is split into two equal halves that line up with the Cancel/OK buttons below: those are
  //  #pushbutton (box-anchor="fill") in a flex row, so they take exactly half the dialog width each.
  //  Matching that split -- rather than hand-tuned margins or stretches -- is what makes the columns
  //  align, and it gives each control group a definite width instead of one squeezing out another.
  //  The 0 4 margin matches the button container's own "5 4", so the halves start at the same x as the
  //  buttons rather than 4 units inboard of them.
  auto halfCell = [](Widget* content) {
    Widget* cell = createRow({}, "0 4", "flex-start");
    cell->node->setAttribute("box-anchor", "hfill");
    cell->addWidget(content);
    return cell;
  };
  Widget* styleRow1 = createRow({}, "0 0", "flex-start");
  styleRow1->node->setAttribute("box-anchor", "hfill");
  styleRow1->addWidget(halfCell(labeled(_("Font"), comboFont)));
  styleRow1->addWidget(halfCell(labeled(_("Size"), spinSize)));
  dialogBody->addWidget(styleRow1);
  // Left half holds Bold + Italic (both fit within one button's width); right half holds Color, so the
  //  "Color" label starts at the same x as the OK button below it.
  Widget* styleRow2 = createRow({}, "0 0", "flex-start");
  styleRow2->node->setAttribute("box-anchor", "hfill");
  Widget* boldItalic = createRow({}, "0 0", "flex-start");
  Widget* boldItem = labeled(_("Bold"), cbBold);
  boldItem->node->setAttribute("margin", "0 16 0 0");  // fixed gap so Italic doesn't crowd Bold
  boldItalic->addWidget(boldItem);
  boldItalic->addWidget(labeled(_("Italic"), cbItalic));
  styleRow2->addWidget(halfCell(boldItalic));
  styleRow2->addWidget(halfCell(labeled(_("Color"), colorPicker)));
  dialogBody->addWidget(styleRow2);

  // color picker must be in the document before setColor works
  colorPicker->setColor(color);

  setTitle(srcNode ? _("Edit Text") : _("Add Text"));
  addButton(_("OK"), [this](){ finish(ACCEPTED); });
  addButton(_("Cancel"), [this](){ finish(CANCELLED); });
}

bool TextBoxDialog::isEmpty() const
{
  return trimStr(textEdit->text()).empty();
}

void TextBoxDialog::applyTo(SvgText* textnode) const
{
  textnode->setText(textEdit->text().c_str());
  textnode->setAttribute("font-family", comboFont->text());
  textnode->setAttribute("font-size", fstring("%g", spinSize->value()).c_str());
  textnode->setAttribute("font-weight", cbBold->isChecked() ? "bold" : "normal");
  textnode->setAttribute("font-style", cbItalic->isChecked() ? "italic" : "normal");
  setSvgFillColor(textnode, colorPicker->color());
}
