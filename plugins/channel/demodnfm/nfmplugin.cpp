#include <QtPlugin>
#include "plugin/pluginapi.h"
#include "nfmplugin.h"
#include "nfmdemodgui.h"

const PluginDescriptor NFMPlugin::m_pluginDescriptor = {
	QString("NFM Demodulator"),
	QString("---"),
	QString("(c) Edouard Griffiths, F4EXB"),
	QString("https://github.com/f4exb/sdrangel"),
	true,
	QString("https://github.com/f4exb/sdrangel")
};

NFMPlugin::NFMPlugin(QObject* parent) :
	QObject(parent)
{
}

const PluginDescriptor& NFMPlugin::getPluginDescriptor() const
{
	return m_pluginDescriptor;
}

void NFMPlugin::initPlugin(PluginAPI* pluginAPI)
{
	m_pluginAPI = pluginAPI;

	// register NFM demodulator
	m_pluginAPI->registerChannel("de.maintech.sdrangelove.channel.nfm", this);
}

PluginGUI* NFMPlugin::createChannel(const QString& channelName, DeviceAPI *deviceAPI)
{
	if(channelName == "de.maintech.sdrangelove.channel.nfm") {
		NFMDemodGUI* gui = NFMDemodGUI::create(m_pluginAPI, deviceAPI);
		m_pluginAPI->registerChannelInstance("de.maintech.sdrangelove.channel.nfm", gui);
//		m_pluginAPI->addChannelRollup(gui);
		return gui;
	} else {
		return NULL;
	}
}

void NFMPlugin::createInstanceNFM(DeviceAPI *deviceAPI)
{
	NFMDemodGUI* gui = NFMDemodGUI::create(m_pluginAPI, deviceAPI);
	m_pluginAPI->registerChannelInstance("de.maintech.sdrangelove.channel.nfm", gui);
//	m_pluginAPI->addChannelRollup(gui);
}
