#pragma once

#include "ugui/widgets.h"
#include "basics.h"

class ColorEditBox;
class TextEdit;
class SvgText;

// Dialog for creating or editing a text box (an SVG <text> element).  Only whole-element styling is
// supported (SVG-native): font family, size, colour, bold (font-weight) and italic (font-style).  The
// caller owns creation/insertion of the SvgText node into the document; this dialog only reads from and
// writes to the node's text content and style attributes.
class TextBoxDialog : public Dialog
{
public:
  // srcNode == NULL: creating a new text box (fields start from defaults / passed-in initial values)
  // srcNode != NULL: editing an existing text box (fields are populated from the node)
  TextBoxDialog(const SvgText* srcNode = NULL);

  // apply the current dialog state (text + style attributes) to the given node
  void applyTo(SvgText* node) const;
  // true if the entered text is empty (caller may want to discard an empty new text box)
  bool isEmpty() const;

  // list of font family names offered in the combo box; index 0 is the bundled UI font ("ui-sans")
  static std::vector<std::string> fontFamilies();

private:
  TextEdit* textEdit;
  ComboBox* comboFont;
  SpinBox* spinSize;
  ColorEditBox* colorPicker;
  CheckBox* cbBold;
  CheckBox* cbItalic;
};
