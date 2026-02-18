#include "guis/GuiNetplaySettings.h"
#include "guis/GuiNetplayLobby.h"
#include "guis/GuiNetplayLan.h"
#include "SAStyle.h"

#include "guis/GuiMsgBox.h"
#include "guis/GuiTextInput.h"
#include "guis/GuiSettings.h"
#include "components/TextComponent.h"
#include "components/SwitchComponent.h"
#include "components/OptionListComponent.h"
#include "NetplayConfig.h"
#include "Window.h"
#include "Log.h"

// ============================================================================
//  Constructor
// ============================================================================

GuiNetplaySettings::GuiNetplaySettings(Window* window)
	: GuiComponent(window),
	  mMenu(window, "NETPLAY SETTINGS")
{
	buildMenu();
	addChild(&mMenu);

	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
	mMenu.setPosition((mSize.x() - mMenu.getSize().x()) / 2,
	                   (mSize.y() - mMenu.getSize().y()) / 2);
}

// ============================================================================
//  buildMenu
// ============================================================================

void GuiNetplaySettings::buildMenu()
{
	NetplayConfig& cfg = NetplayConfig::get();

	// Subtitle shows current config at a glance
	mMenu.setSubtitle(cfg.getSubtitleText(), SA_SUBTITLE_COLOR);

	ComponentListRow row;

	// ---- PLAYER NAME ----
	row.elements.clear();
	row.addElement(std::make_shared<TextComponent>(mWindow,
		"PLAYER NAME", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
	row.addElement(std::make_shared<TextComponent>(mWindow,
		Utils::String::toUpper(cfg.nickname), saFont(FONT_SIZE_MEDIUM), SA_SUBTITLE_COLOR), false);
	row.makeAcceptInputHandler([this] { changePlayerName(); });
	mMenu.addRow(row);

	// ---- PLAY MODE ----
	row.elements.clear();
	row.addElement(std::make_shared<TextComponent>(mWindow,
		"PLAY MODE", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
	row.addElement(std::make_shared<TextComponent>(mWindow,
		Utils::String::toUpper(cfg.getModeLabel()), saFont(FONT_SIZE_MEDIUM), SA_SUBTITLE_COLOR), false);
	row.makeAcceptInputHandler([this] { changeMode(); });
	mMenu.addRow(row);

	// ---- CONNECTION TYPE (online mode only) ----
	if (cfg.mode == "online")
	{
		row.elements.clear();
		row.addElement(std::make_shared<TextComponent>(mWindow,
			"CONNECTION TYPE", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
		row.addElement(std::make_shared<TextComponent>(mWindow,
			Utils::String::toUpper(cfg.getOnlineMethodLabel()), saFont(FONT_SIZE_MEDIUM), SA_SUBTITLE_COLOR), false);
		row.makeAcceptInputHandler([this] { changeConnectionType(); });
		mMenu.addRow(row);
	}

	// ---- PORT ----
	row.elements.clear();
	row.addElement(std::make_shared<TextComponent>(mWindow,
		"PORT", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
	row.addElement(std::make_shared<TextComponent>(mWindow,
		cfg.port, saFont(FONT_SIZE_MEDIUM), SA_SUBTITLE_COLOR), false);
	row.makeAcceptInputHandler([this] { changePort(); });
	mMenu.addRow(row);

	// ---- BROWSE ONLINE GAMES ---- (Phase 3 placeholder)
	row.elements.clear();
	row.addElement(std::make_shared<TextComponent>(mWindow,
		"BROWSE ONLINE GAMES", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
	row.addElement(makeArrow(mWindow), false);
	row.makeAcceptInputHandler([this] { browseOnlineGames(); });
	mMenu.addRow(row);

	// ---- BROWSE LAN GAMES ---- (Phase 3 placeholder)
	row.elements.clear();
	row.addElement(std::make_shared<TextComponent>(mWindow,
		"BROWSE LAN GAMES", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
	row.addElement(makeArrow(mWindow), false);
	row.makeAcceptInputHandler([this] { browseLanGames(); });
	mMenu.addRow(row);

	// ---- ADVANCED OPTIONS ----
	row.elements.clear();
	row.addElement(std::make_shared<TextComponent>(mWindow,
		"ADVANCED OPTIONS", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
	row.addElement(makeArrow(mWindow), false);
	row.makeAcceptInputHandler([this] { openAdvancedOptions(); });
	mMenu.addRow(row);

	// ---- RESTORE DEFAULTS ----
	row.elements.clear();
	row.addElement(std::make_shared<TextComponent>(mWindow,
		"RESTORE DEFAULTS", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
	row.makeAcceptInputHandler([this] { restoreDefaults(); });
	mMenu.addRow(row);
}

// ============================================================================
//  rebuildMenu — close this GUI and open a fresh one
// ============================================================================

void GuiNetplaySettings::rebuildMenu()
{
	Window* window = mWindow;
	delete this;
	window->pushGui(new GuiNetplaySettings(window));
}

// ============================================================================
//  changePlayerName — uses GuiTextInput
// ============================================================================

void GuiNetplaySettings::changePlayerName()
{
	NetplayConfig& cfg = NetplayConfig::get();
	Window* window = mWindow;

	auto callback = [window](const std::string& result)
	{
		std::string cleaned = NetplayConfig::sanitizeNickname(result);
		if (cleaned.empty())
			cleaned = "Player";
		NetplayConfig::get().nickname = cleaned;
		NetplayConfig::get().save();
	};

	mWindow->pushGui(new GuiTextInput(mWindow,
		"ENTER PLAYER NAME:", cfg.nickname, callback));
}

// ============================================================================
//  changeMode — Online / LAN (toggle)
// ============================================================================

void GuiNetplaySettings::changeMode()
{
	NetplayConfig& cfg = NetplayConfig::get();

	if (cfg.mode == "online")
	{
		cfg.mode = "lan";
		cfg.save();
		rebuildMenu();
	}
	else
	{
		cfg.mode = "online";
		cfg.save();
		rebuildMenu();
	}
}

// ============================================================================
//  changeConnectionType — Relay / Direct (toggle)
// ============================================================================

void GuiNetplaySettings::changeConnectionType()
{
	NetplayConfig& cfg = NetplayConfig::get();
	cfg.onlineMethod = (cfg.onlineMethod == "relay") ? "direct" : "relay";
	cfg.save();
	rebuildMenu();
}

// ============================================================================
//  changePort — uses GuiTextInput
// ============================================================================

void GuiNetplaySettings::changePort()
{
	NetplayConfig& cfg = NetplayConfig::get();
	Window* window = mWindow;

	auto callback = [window](const std::string& result)
	{
		// Strip non-digits
		std::string cleaned;
		for (char c : result)
			if (std::isdigit(c)) cleaned += c;

		if (cleaned.empty())
			cleaned = "55435";

		int port = 0;
		try { port = std::stoi(cleaned); } catch (...) { port = 0; }

		if (port < 1 || port > 65535)
		{
			window->pushGui(new GuiMsgBox(window,
				"INVALID PORT NUMBER\n\n"
				"PLEASE ENTER A NUMBER BETWEEN 1 AND 65535.\n"
				"THE DEFAULT PORT IS 55435.",
				"OK", nullptr));
			return;
		}

		NetplayConfig::get().port = cleaned;
		NetplayConfig::get().save();
	};

	mWindow->pushGui(new GuiTextInput(mWindow,
		"ENTER PORT NUMBER (1-65535):", cfg.port, callback));
}

// ============================================================================
//  openAdvancedOptions — interactive submenu using GuiSettings
// ============================================================================

void GuiNetplaySettings::openAdvancedOptions()
{
	Window* window = mWindow;
	NetplayConfig& cfg = NetplayConfig::get();

	auto s = new GuiSettings(window, "ADVANCED OPTIONS");

	// ---- PUBLIC ANNOUNCE ----
	auto publicAnnounce = std::make_shared<SwitchComponent>(window);
	publicAnnounce->setState(cfg.publicAnnounce == "true" || cfg.publicAnnounce == "auto");
	s->addWithLabel("PUBLIC ANNOUNCE", publicAnnounce);

	// ---- NAT TRAVERSAL ----
	auto natTraversal = std::make_shared<SwitchComponent>(window);
	natTraversal->setState(cfg.natTraversal == "true");
	s->addWithLabel("NAT TRAVERSAL", natTraversal);

	// ---- ALLOW SLOWER DEVICES ----
	auto allowSlaves = std::make_shared<SwitchComponent>(window);
	allowSlaves->setState(cfg.allowSlaves == "true");
	s->addWithLabel("ALLOW SLOWER DEVICES", allowSlaves);

	// ---- MAX PLAYERS ----
	auto maxConn = std::make_shared<OptionListComponent<std::string>>(window, "MAX PLAYERS", false);
	maxConn->add("2", "2", cfg.maxConnections == "2");
	maxConn->add("3", "3", cfg.maxConnections == "3");
	maxConn->add("4", "4", cfg.maxConnections == "4");
	s->addWithLabel("MAX PLAYERS", maxConn);

	// ---- GAME PASSWORD ----
	ComponentListRow pwRow;
	pwRow.addElement(std::make_shared<TextComponent>(window,
		std::string("GAME PASSWORD: ") + (cfg.password.empty() ? "NOT SET" : "SET"),
		saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
	pwRow.makeAcceptInputHandler([window]
	{
		window->pushGui(new GuiTextInput(window, "GAME PASSWORD",
			NetplayConfig::get().password,
			[window](const std::string& val)
			{
				NetplayConfig::get().password = val;
				NetplayConfig::get().save();
				window->pushGui(new GuiMsgBox(window,
					"PASSWORD " + std::string(val.empty() ? "CLEARED" : "SET") + ".",
					"OK", nullptr));
			}, true)); // true = password mode
	});
	s->addRow(pwRow);

	// ---- SPECTATOR PASSWORD ----
	ComponentListRow specRow;
	specRow.addElement(std::make_shared<TextComponent>(window,
		std::string("SPECTATOR PASSWORD: ") + (cfg.spectatePassword.empty() ? "NOT SET" : "SET"),
		saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
	specRow.makeAcceptInputHandler([window]
	{
		window->pushGui(new GuiTextInput(window, "SPECTATOR PASSWORD",
			NetplayConfig::get().spectatePassword,
			[window](const std::string& val)
			{
				NetplayConfig::get().spectatePassword = val;
				NetplayConfig::get().save();
				window->pushGui(new GuiMsgBox(window,
					"SPECTATOR PASSWORD " + std::string(val.empty() ? "CLEARED" : "SET") + ".",
					"OK", nullptr));
			}, true));
	});
	s->addRow(specRow);

	// ---- SAVE ON CLOSE ----
	s->addSaveFunc([publicAnnounce, natTraversal, allowSlaves, maxConn]
	{
		NetplayConfig& cfg = NetplayConfig::get();
		cfg.publicAnnounce = publicAnnounce->getState() ? "true" : "false";
		cfg.natTraversal = natTraversal->getState() ? "true" : "false";
		cfg.allowSlaves = allowSlaves->getState() ? "true" : "false";
		cfg.maxConnections = maxConn->getSelected();
		cfg.save();
	});

	window->pushGui(s);
}

// ============================================================================
//  restoreDefaults
// ============================================================================

void GuiNetplaySettings::restoreDefaults()
{
	Window* window = mWindow;

	mWindow->pushGui(new GuiMsgBox(mWindow,
		"RESTORE DEFAULT SETTINGS?\n\n"
		"THIS WILL RESET ALL ADVANCED SETTINGS TO THEIR\n"
		"RECOMMENDED VALUES.\n\n"
		"YOUR PLAYER NAME AND PORT WILL NOT BE CHANGED.",
		"YES", [window]
		{
			NetplayConfig::get().resetAdvancedToDefaults();
			NetplayConfig::get().save();
			window->pushGui(new GuiMsgBox(window,
				"SETTINGS RESTORED TO DEFAULTS.", "OK", nullptr));
		},
		"NO", nullptr));
}

// ============================================================================
//  browseOnlineGames / browseLanGames
// ============================================================================

void GuiNetplaySettings::browseOnlineGames()
{
	mWindow->pushGui(new GuiNetplayLobby(mWindow));
}

void GuiNetplaySettings::browseLanGames()
{
	mWindow->pushGui(new GuiNetplayLan(mWindow));
}

// ============================================================================
//  Input
// ============================================================================

bool GuiNetplaySettings::input(InputConfig* config, Input input)
{
	if (config->isMappedTo("b", input) && input.value != 0)
	{
		delete this;
		return true;
	}

	return mMenu.input(config, input);
}

std::vector<HelpPrompt> GuiNetplaySettings::getHelpPrompts()
{
	auto prompts = mMenu.getHelpPrompts();
	prompts.push_back(HelpPrompt("b", "back"));
	return prompts;
}

HelpStyle GuiNetplaySettings::getHelpStyle()
{
	HelpStyle style = HelpStyle();
	return style;
}
