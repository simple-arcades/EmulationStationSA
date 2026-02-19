#pragma once
#ifndef ES_APP_GUIS_GUI_SETTINGS_H
#define ES_APP_GUIS_GUI_SETTINGS_H

#include "components/MenuComponent.h"

// This is just a really simple template for a GUI that calls some save functions when closed.
class GuiSettings : public GuiComponent
{
public:
	GuiSettings(Window* window, const char* title);
	virtual ~GuiSettings(); // just calls save();

	void save();
	inline void addRow(const ComponentListRow& row) { mMenu.addRow(row); };
	inline void addWithLabel(const std::string& label, const std::shared_ptr<GuiComponent>& comp) { mMenu.addWithLabel(label, comp); };
	inline void addSaveFunc(const std::function<void()>& func) { mSaveFuncs.push_back(func); };

	// Show or hide a row by index. Delegates to MenuComponent.
	inline void setRowVisible(int rowIndex, bool visible) { mMenu.setRowVisible(rowIndex, visible); };

	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;
	HelpStyle getHelpStyle() override;

private:
	MenuComponent mMenu;
	std::vector< std::function<void()> > mSaveFuncs;
};

#endif // ES_APP_GUIS_GUI_SETTINGS_H