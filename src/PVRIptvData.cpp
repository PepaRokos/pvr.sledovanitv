/*
 *      Copyright (C) 2014 Josef Rokos
 *      http://github.com/PepaRokos/xbmc-pvr-addons/
 *
 *      Copyright (C) 2011 Pulse-Eight
 *      http://www.pulse-eight.com/
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <sstream>
#include <string>
#include <fstream>
#include <iostream>
#include <map>
#include <json/json.h>

#include "PVRIptvData.h"
#include "apimanager.h"

using namespace std;
using namespace ADDON;

extern bool g_bHdEnabled;

PVRIptvData::PVRIptvData(void)
{
  m_iLastStart    = 0;
  m_iLastEnd      = 0;

  m_bEGPLoaded = false;
  m_bIsPlaying = false;

  m_manager.login();

  if (LoadPlayList())
  {
    XBMC->QueueNotification(QUEUE_INFO, "%d channels loaded.", m_channels.size());
  }

  m_bKeepAlive = true;
  CreateThread();
}

void *PVRIptvData::Process(void)
{
  XBMC->Log(LOG_DEBUG, "keepAlive:: thread started");
  unsigned int counter = 0;
  while (m_bKeepAlive)
  {
    if (counter >= 20000)
    {
      XBMC->Log(LOG_DEBUG, "keepAlive:: trigger");
      counter = 0;
      if (!m_manager.keepAlive())
      {
        ThreadSafeLogin();
      }
    }

    counter += 1000;
    Sleep(1000);
  }
  XBMC->Log(LOG_DEBUG, "keepAlive:: thread stopped");
  return NULL;
}

void PVRIptvData::ThreadSafeLogin()
{
  P8PLATFORM::CLockObject critical(m_mutexLogin);

  m_manager.login();
}

PVRIptvData::~PVRIptvData(void)
{
  m_bKeepAlive = false;
  if (IsRunning())
  {
    StopThread();
  }

  m_channels.clear();
  m_groups.clear();
  m_epg.clear();  
}

bool PVRIptvData::LoadEPG(time_t iStart, time_t iEnd) 
{
  P8PLATFORM::CLockObject critical(m_mutex);

  if (!m_manager.isLoggedIn())
  {
    XBMC->Log(LOG_NOTICE, "Not logged in. EPG not loaded.");
    m_bEGPLoaded = true;
    return false;
  }

  /*if (!m_manager.keepAlive())
  {
    ThreadSafeLogin();
  }*/

  auto lmLoad = [this](time_t start, int dur) -> bool {
    std::string epgString = m_manager.getEpg(start, dur);

    Json::Reader reader;
    Json::Value root;

    if (!reader.parse(epgString, root))
    {
      XBMC->Log(LOG_NOTICE, "Cannot parse EPG data. EPG not loaded.");
      return false;
    }

    Json::Value channels = root["channels"];
    Json::Value::Members chIds = channels.getMemberNames();
    for (Json::Value::Members::iterator i = chIds.begin(); i != chIds.end(); i++)
    {
      std::string strChId = *i;

      PVRIptvChannel* iptvchannel = FindChannel(strChId, "");
      if (iptvchannel != NULL)
      {
        PVRIptvEpgChannel *epgChannel = NULL;
        bool bFound = false;

        auto epgCh = std::find_if(m_epg.begin(), m_epg.end(), [strChId](PVRIptvEpgChannel &ch) {
          return ch.strId == strChId;
        });

        if (epgCh != m_epg.end())
        {
          epgChannel = &*epgCh;
          bFound = true;
        }
        else
        {
          epgChannel = new PVRIptvEpgChannel;
          epgChannel->strId = strChId;
          epgChannel->strName = iptvchannel->strChannelName;
        }

        Json::Value epgData = channels[strChId];
        for (unsigned int j = 0; j < epgData.size(); j++)
        {
          Json::Value epgEntry = epgData[j];

          time_t timeStamp = time(NULL);

          PVRIptvEpgEntry iptventry;
          iptventry.iGenreType = 0;
          iptventry.iGenreSubType = 0;
          iptventry.iChannelId = iptvchannel->iUniqueId;
          iptventry.strTitle = epgEntry.get("title", "").asString();
          iptventry.strPlot = epgEntry.get("description", "").asString();
          iptventry.startTime = ParseDateTime(epgEntry.get("startTime", "").asString());
          iptventry.iBroadcastId = iptventry.startTime;
          iptventry.endTime = ParseDateTime(epgEntry.get("endTime", "").asString());

          XBMC->Log(LOG_DEBUG, "Loading TV show: %s - %s", strChId.c_str(), iptventry.strTitle.c_str());

          epgChannel->epg.push_back(iptventry);
        }

        if (!bFound)
        {
          m_epg.push_back(*epgChannel);
          delete epgChannel;
        }
      }

    }

    return true;
  };

  int duration = (iEnd - iStart)/60;

  if (duration < MAX_EPG_DURATION)
  {
    if (m_epg.size() > 0)
    {
      m_epg.clear();
    }

    m_bEGPLoaded = lmLoad(iStart, duration);
  }
  else
  {
    int loadCount = duration/MAX_EPG_DURATION;
    int startLoad = iStart;
    bool loaded = true;
    for (int i = 0; i < loadCount; i++)
    {
      loaded &= lmLoad(startLoad, MAX_EPG_DURATION);
      startLoad = startLoad + (MAX_EPG_DURATION * 60);
    }

    m_bEGPLoaded = loaded;
  }

  XBMC->Log(LOG_NOTICE, "EPG Loaded.");

  return true;
}

bool PVRIptvData::LoadRecordings()
{
  P8PLATFORM::CLockObject critical(m_mutex);

  if (!m_manager.isLoggedIn())
  {
    XBMC->Log(LOG_NOTICE, "Not logged in. Recordings not loaded.");
    return false;
  }

  if (m_recordings.size() > 0)
  {
    m_recordings.clear();
  }

  if (m_timers.size() > 0)
  {
    m_timers.clear();
  }

  std::string strRecordings = m_manager.getPvr();

  Json::Reader reader;
  Json::Value root;

  if (!reader.parse(strRecordings, root))
  {
    XBMC->Log(LOG_NOTICE, "Cannot parse recordings.");
    return false;
  }

  Json::Value records = root["records"];
  for (unsigned int i = 0; i < records.size(); i++)
  {
    Json::Value record = records[i];
    PVRIptvChannel* channel = FindChannel(record.get("channel", "").asString(), "");
    PVRIptvRecording iptvrecording;
    PVRIptvTimer iptvtimer;
    time_t startTime = ParseDateTime(record.get("startTime", "").asString());
    int duration = record.get("duration", 0).asInt();
    time_t now;
    time(&now);

    if ((startTime + duration) < now)
    {
      char buf[256];
      sprintf(buf, "%d", record.get("id", 0).asInt());
      iptvrecording.strRecordId = buf;
      iptvrecording.strTitle = record.get("title", "").asString();

      XBMC->Log(LOG_DEBUG, "Loading recording '%s'", iptvrecording.strTitle.c_str());

      if (channel != NULL)
      {
        iptvrecording.strChannelName = channel->strChannelName;
      }
      iptvrecording.startTime = startTime;
      iptvrecording.strPlotOutline = record.get("event", "").get("description", "").asString();
      iptvrecording.duration = duration;

      m_recordings.push_back(iptvrecording);
    }
    else
    {
      iptvtimer.iClientIndex = record.get("id", 0).asInt();
      if (channel != NULL)
      {
        iptvtimer.iClientChannelUid = channel->iUniqueId;
      }
      iptvtimer.startTime = ParseDateTime(record.get("startTime", "").asString());
      iptvtimer.endTime = iptvtimer.startTime + record.get("duration", 0).asInt();

      if (startTime < now && (startTime + duration) >= now)
      {
        iptvtimer.state = PVR_TIMER_STATE_RECORDING;
      }
      else
      {
        iptvtimer.state = PVR_TIMER_STATE_SCHEDULED;
      }
      iptvtimer.strTitle = record.get("title", "").asString();

      XBMC->Log(LOG_DEBUG, "Loading timer '%s'", iptvtimer.strTitle.c_str());

      m_timers.push_back(iptvtimer);
    }

  }

  return true;
}

bool PVRIptvData::LoadPlayList(void) 
{
  if (!m_manager.isLoggedIn())
  {
    XBMC->Log(LOG_NOTICE, "Not logged in. Channels not loaded.");
    return false;
  }

  XBMC->Log(LOG_DEBUG, "Loadin playlist");

  std::string playlist = m_manager.getPlaylist();

  Json::Reader reader;
  Json::Value root;

  if (!reader.parse(playlist, root))
  {
    XBMC->Log(LOG_NOTICE, "Cannot parse playlist.");
    return false;
  }
  
  std::string qualities = m_manager.getStreamQualities();
  XBMC->Log(LOG_DEBUG, "Stream qualities: %s", qualities.c_str());

  Json::Value channels = root["channels"];
  for (unsigned int i = 0; i < channels.size(); i++)
  {
    Json::Value channel = channels[i];
    PVRIptvChannel iptvchan;

    iptvchan.strTvgId = channel.get("id", "").asString();
    iptvchan.strChannelName = channel.get("name", "").asString();
    iptvchan.strStreamURL = channel.get("url", "").asString();
    
    XBMC->Log(LOG_DEBUG, "Channel %s, URL: %s", iptvchan.strChannelName.c_str(), iptvchan.strStreamURL.c_str());
    
    std::string strUrl = iptvchan.strStreamURL;

    if (g_bHdEnabled)
    {
      size_t qIndex = strUrl.find("quality");
      strUrl.replace(strUrl.begin() + qIndex, strUrl.end(), "quality=40");
    }

    iptvchan.strStreamURL = strUrl;
    iptvchan.iUniqueId = GetChannelId(iptvchan.strChannelName.c_str(), channel.get("type", "").asString().c_str());
    iptvchan.iChannelNumber = i + 1;
    iptvchan.strLogoPath = channel.get("logoUrl", "").asString();
    iptvchan.bRadio = channel.get("type", "").asString() != "tv";

    m_channels.push_back(iptvchan);
  }

  XBMC->Log(LOG_NOTICE, "Loaded %d channels.", m_channels.size());
  return true;
}

int PVRIptvData::GetChannelsAmount(void)
{
  return m_channels.size();
}

PVR_ERROR PVRIptvData::GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  for (unsigned int iChannelPtr = 0; iChannelPtr < m_channels.size(); iChannelPtr++)
  {
    PVRIptvChannel &channel = m_channels.at(iChannelPtr);
    if (channel.bRadio == bRadio)
    {
      PVR_CHANNEL xbmcChannel;
      memset(&xbmcChannel, 0, sizeof(PVR_CHANNEL));

      xbmcChannel.iUniqueId         = channel.iUniqueId;
      xbmcChannel.bIsRadio          = channel.bRadio;
      xbmcChannel.iChannelNumber    = channel.iChannelNumber;
      strncpy(xbmcChannel.strChannelName, channel.strChannelName.c_str(), sizeof(xbmcChannel.strChannelName) - 1);
      xbmcChannel.iEncryptionSystem = channel.iEncryptionSystem;
      strncpy(xbmcChannel.strIconPath, channel.strLogoPath.c_str(), sizeof(xbmcChannel.strIconPath) - 1);
      xbmcChannel.bIsHidden         = false;

      PVR->TransferChannelEntry(handle, &xbmcChannel);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

bool PVRIptvData::GetChannel(const PVR_CHANNEL &channel, PVRIptvChannel &myChannel)
{
  for (unsigned int iChannelPtr = 0; iChannelPtr < m_channels.size(); iChannelPtr++)
  {
    PVRIptvChannel &thisChannel = m_channels.at(iChannelPtr);
    if (thisChannel.iUniqueId == (int) channel.iUniqueId)
    {
      myChannel.iUniqueId         = thisChannel.iUniqueId;
      myChannel.bRadio            = thisChannel.bRadio;
      myChannel.iChannelNumber    = thisChannel.iChannelNumber;
      myChannel.iEncryptionSystem = thisChannel.iEncryptionSystem;
      myChannel.strChannelName    = thisChannel.strChannelName;
      myChannel.strLogoPath       = thisChannel.strLogoPath;
      myChannel.strStreamURL      = thisChannel.strStreamURL;
      myChannel.properties        = thisChannel.properties;

      return true;
    }
  }

  return false;
}

bool PVRIptvData::GetRecording(const PVR_RECORDING &recording, PVRIptvRecording &myRecording)
{
  for (unsigned int i = 0; i < m_recordings.size(); i++)
  {
    PVRIptvRecording &thisRec = m_recordings.at(i);
    if (strcmp(recording.strRecordingId, thisRec.strRecordId.c_str()) == 0)
    {
      myRecording.strRecordId      = thisRec.strRecordId;
      myRecording.strTitle         = thisRec.strTitle;
      myRecording.strStreamUrl     = thisRec.strStreamUrl;
      myRecording.strPlotOutline   = thisRec.strPlotOutline;
      myRecording.strPlot          = thisRec.strPlot;
      myRecording.strChannelName   = thisRec.strChannelName;
      myRecording.startTime        = thisRec.startTime;
      myRecording.duration         = thisRec.duration;

      return true;
    }
  }

  return false;
}

int PVRIptvData::GetChannelGroupsAmount(void)
{
  return m_groups.size();
}

PVR_ERROR PVRIptvData::GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  for (unsigned int iGroupPtr = 0; iGroupPtr < m_groups.size(); iGroupPtr++)
  {
    PVRIptvChannelGroup &group = m_groups.at(iGroupPtr);
    if (group.bRadio == bRadio)
    {
      PVR_CHANNEL_GROUP xbmcGroup;
      memset(&xbmcGroup, 0, sizeof(PVR_CHANNEL_GROUP));

      xbmcGroup.bIsRadio = bRadio;
      strncpy(xbmcGroup.strGroupName, group.strGroupName.c_str(), sizeof(xbmcGroup.strGroupName) - 1);

      PVR->TransferChannelGroup(handle, &xbmcGroup);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  PVRIptvChannelGroup *myGroup;
  if ((myGroup = FindGroup(group.strGroupName)) != NULL)
  {
    for (unsigned int iPtr = 0; iPtr < myGroup->members.size(); iPtr++)
    {
      int iIndex = myGroup->members.at(iPtr);
      if (iIndex < 0 || iIndex >= (int) m_channels.size())
        continue;

      PVRIptvChannel &channel = m_channels.at(iIndex);
      PVR_CHANNEL_GROUP_MEMBER xbmcGroupMember;
      memset(&xbmcGroupMember, 0, sizeof(PVR_CHANNEL_GROUP_MEMBER));

      strncpy(xbmcGroupMember.strGroupName, group.strGroupName, sizeof(xbmcGroupMember.strGroupName) - 1);
      xbmcGroupMember.iChannelUniqueId = channel.iUniqueId;
      xbmcGroupMember.iChannelNumber   = channel.iChannelNumber;

      PVR->TransferChannelGroupMember(handle, &xbmcGroupMember);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
  XBMC->Log(LOG_DEBUG, "Read EPG for channel %s", channel.strChannelName);
  vector<PVRIptvChannel>::iterator myChannel;
  for (myChannel = m_channels.begin(); myChannel < m_channels.end(); myChannel++)
  {
    if (myChannel->iUniqueId != (int) channel.iUniqueId)
    {
      continue;
    }

    XBMC->Log(LOG_DEBUG, "Read EPG last start %d, start %d, last end %d, end %d", m_iLastStart, iStart, m_iLastEnd, iEnd);
    if (!m_bEGPLoaded || iStart > m_iLastStart || iEnd > m_iLastEnd) 
    {
      if (LoadEPG(iStart, iEnd))
      {
        m_iLastStart = iStart;
        m_iLastEnd = iEnd;
      }
    }

    PVRIptvEpgChannel *epg;
    if ((epg = FindEpgForChannel(*myChannel)) == NULL || epg->epg.size() == 0)
    {
      XBMC->Log(LOG_DEBUG, "EPG not found");
      return PVR_ERROR_NO_ERROR;
    }

    vector<PVRIptvEpgEntry>::iterator myTag;
    for (myTag = epg->epg.begin(); myTag < epg->epg.end(); myTag++)
    {
      EPG_TAG tag;
      memset(&tag, 0, sizeof(EPG_TAG));

      tag.iUniqueBroadcastId  = myTag->iBroadcastId;
      tag.strTitle            = myTag->strTitle.c_str();
      tag.iUniqueChannelId    = myTag->iChannelId;
      tag.startTime           = myTag->startTime;
      tag.endTime             = myTag->endTime;
      tag.strPlotOutline      = myTag->strPlotOutline.c_str();
      tag.strPlot             = myTag->strPlot.c_str();
      tag.strIconPath         = myTag->strIconPath.c_str();
      tag.iGenreType          = EPG_GENRE_USE_STRING;        //myTag.iGenreType;
      tag.iGenreSubType       = 0;                           //myTag.iGenreSubType;
      tag.strGenreDescription = myTag->strGenreString.c_str();

      PVR->TransferEpgEntry(handle, &tag);
    }

    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_NO_ERROR;
}

int PVRIptvData::ParseDateTime(std::string strDate)
{
  struct tm timeinfo;
  memset(&timeinfo, 0, sizeof(tm));

  sscanf(strDate.c_str(), "%04d-%02d-%02d %02d:%02d", &timeinfo.tm_year, &timeinfo.tm_mon, &timeinfo.tm_mday, &timeinfo.tm_hour, &timeinfo.tm_min);
  timeinfo.tm_sec = 0;

  timeinfo.tm_mon  -= 1;
  timeinfo.tm_year -= 1900;
  timeinfo.tm_isdst = -1;

  return mktime(&timeinfo);
}


PVRIptvChannel * PVRIptvData::FindChannel(const std::string &strId, const std::string &strName)
{
  CStdString strTvgName = strName;
  strTvgName.Replace(' ', '_');

  vector<PVRIptvChannel>::iterator it;
  for(it = m_channels.begin(); it < m_channels.end(); it++)
  {
  if (it->strTvgId == strId)
    {
      return &*it;
    }
    if (strTvgName == "") 
    {
      continue;
    }
    if (it->strTvgName == strTvgName)
    {
      return &*it;
    }
    if (it->strChannelName == strName)
    {
      return &*it;
    }
  }

  return NULL;
}

PVRIptvChannel *PVRIptvData::FindChannel(int iChannelUid)
{
  vector<PVRIptvChannel>::iterator it;
  for (it = m_channels.begin(); it != m_channels.end(); it++)
  {
    if (it->iUniqueId == iChannelUid)
    {
      return &*it;
    }
  }

  return NULL;
}

PVRIptvChannelGroup * PVRIptvData::FindGroup(const std::string &strName)
{
  vector<PVRIptvChannelGroup>::iterator it;
  for(it = m_groups.begin(); it < m_groups.end(); it++)
  {
    if (it->strGroupName == strName)
    {
      return &*it;
    }
  }

  return NULL;
}

PVRIptvEpgChannel * PVRIptvData::FindEpg(const std::string &strId)
{
  vector<PVRIptvEpgChannel>::iterator it;
  for(it = m_epg.begin(); it < m_epg.end(); it++)
  {
    if (it->strId == strId)
    {
      return &*it;
    }
  }

  return NULL;
}

string PVRIptvData::FindTvShowId(const PVRIptvChannel &channel, time_t iStart, time_t iEnd)
{
  std::string resp = m_manager.getEventId(channel.strTvgId, iStart, iEnd);

  Json::Reader reader;
  Json::Value root;

  if (!reader.parse(resp, root))
  {
    XBMC->Log(LOG_NOTICE, "Cannot parse EPG.");
    return "";
  }

  if (root.get("status", 0).asInt() == 0)
  {
    XBMC->Log(LOG_DEBUG, "Returned: %s", resp.c_str());
    return "";
  }

  Json::Value::Members ch = root["channels"].getMemberNames();
  std::string strChannelId = ch[0];

  unsigned int i = 0;
  Json::Value event = root["channels"][strChannelId][i];

  return event.get("eventId", "").asString();
}

PVRIptvEpgChannel * PVRIptvData::FindEpgForChannel(PVRIptvChannel &channel)
{
  vector<PVRIptvEpgChannel>::iterator it;
  for(it = m_epg.begin(); it < m_epg.end(); it++)
  {
    if (it->strId == channel.strTvgId)
    {
      return &*it;
    }
    CStdString strName = it->strName;
    strName.Replace(' ', '_');
    if (strName == channel.strTvgName
      || it->strName == channel.strTvgName)
    {
      return &*it;
    }
    if (it->strName == channel.strChannelName)
    {
      return &*it;
    }
  }

  return NULL;
}

int PVRIptvData::GetRecordingsAmount()
{
  return m_recordings.size();
}

int PVRIptvData::GetChannelId(const char * strChannelName, const char * strStreamUrl) 
{
  std::string concat(strChannelName);
  concat.append(strStreamUrl);

  const char* strString = concat.c_str();
  int iId = 0;
  int c;
  while (c = *strString++)
    iId = ((iId << 5) + iId) + c; /* iId * 33 + c */

  return abs(iId);
}

string PVRIptvData::GetAvailability(const EPG_TAG *epgTag)
{
  PVRIptvChannel *channel = FindChannel(epgTag->iUniqueChannelId);

  if (channel == NULL)
  {
    XBMC->Log(LOG_NOTICE, "Cannot find channel.");
    return "";
  }

  std::string resp = m_manager.getEventId(channel->strTvgId, epgTag->startTime, epgTag->endTime);

  Json::Reader reader;
  Json::Value root;

  if (!reader.parse(resp, root))
  {
    XBMC->Log(LOG_NOTICE, "Cannot parse EPG.");
    return "";
  }

  if (root.get("status", 0).asInt() == 0)
  {
    XBMC->Log(LOG_DEBUG, "Returned: %s", resp.c_str());
    return "";
  }

  Json::Value::Members ch = root["channels"].getMemberNames();
  std::string strChannelId = ch[0];

  unsigned int i = 0;
  Json::Value event = root["channels"][strChannelId][i];

  return event.get("availability", "").asString();
}


PVR_ERROR PVRIptvData::GetRecordings(ADDON_HANDLE handle)
{
  if (!LoadRecordings())
  {
    return PVR_ERROR_NO_ERROR;
  }

  for (int i = 0; i < m_recordings.size(); i++)
  {
    PVRIptvRecording &rec = m_recordings.at(i);

    PVR_RECORDING xbmcRecord;
    memset(&xbmcRecord, 0, sizeof(PVR_RECORDING));

    strncpy(xbmcRecord.strRecordingId, rec.strRecordId.c_str(), sizeof(xbmcRecord.strRecordingId) - 1);
    strncpy(xbmcRecord.strTitle, rec.strTitle.c_str(), sizeof(xbmcRecord.strTitle) - 1);
    strncpy(xbmcRecord.strChannelName, rec.strChannelName.c_str(), sizeof(xbmcRecord.strChannelName) - 1);
    xbmcRecord.recordingTime = rec.startTime;
    strncpy(xbmcRecord.strPlotOutline, rec.strPlotOutline.c_str(), sizeof(xbmcRecord.strPlotOutline) - 1);
    strncpy(xbmcRecord.strPlot, rec.strPlotOutline.c_str(), sizeof(xbmcRecord.strPlot) - 1);
    xbmcRecord.iDuration = rec.duration;

    PVR->TransferRecordingEntry(handle, &xbmcRecord);
  }

  return PVR_ERROR_NO_ERROR;
}

int PVRIptvData::GetTimersAmount()
{
  return m_timers.size();
}


PVR_ERROR PVRIptvData::GetTimers(ADDON_HANDLE handle)
{
  if (m_recordings.empty() && m_timers.empty())
  {
    if (!LoadRecordings())
    {
      return PVR_ERROR_NO_ERROR;
    }
  }

  for (int i = 0; i < m_timers.size(); i++)
  {
    PVRIptvTimer &timer = m_timers.at(i);

    PVR_TIMER xbmcTimer;
    memset(&xbmcTimer, 0, sizeof(PVR_TIMER));

    xbmcTimer.iClientIndex = timer.iClientIndex;
    xbmcTimer.iClientChannelUid = timer.iClientChannelUid;
    xbmcTimer.startTime = timer.startTime;
    xbmcTimer.endTime = timer.endTime;
    xbmcTimer.state = timer.state;
    strncpy(xbmcTimer.strTitle, timer.strTitle.c_str(), sizeof(xbmcTimer.strTitle) - 1);
    strncpy(xbmcTimer.strSummary, timer.strSummary.c_str(), sizeof(xbmcTimer.strSummary) - 1);

    PVR->TransferTimerEntry(handle, &xbmcTimer);
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::AddTimer(const PVR_TIMER &timer)
{
  PVRIptvChannel *channel = FindChannel(timer.iClientChannelUid);
  if (channel == NULL)
  {
    XBMC->Log(LOG_DEBUG, "channel not found");
    return PVR_ERROR_SERVER_ERROR;
  }

  string strEventId = FindTvShowId(*channel, timer.startTime, timer.endTime);
  if (strEventId.empty())
  {
    XBMC->Log(LOG_DEBUG, "event not found");
    return PVR_ERROR_SERVER_ERROR;
  }

  bool sucess = m_manager.addTimer(strEventId);
  if (sucess)
  {
    PVR->TriggerRecordingUpdate();
    PVR->TriggerTimerUpdate();
  }

  return sucess ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR PVRIptvData::DeleteRecord(const string &strRecordId)
{
  bool sucess = m_manager.deleteRecord(strRecordId);
  if (sucess)
  {
    PVR->TriggerRecordingUpdate();
    PVR->TriggerTimerUpdate();
  }

  return sucess ? PVR_ERROR_NO_ERROR : PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR PVRIptvData::DeleteRecord(int iRecordId)
{
  char buff[128];
  std::string strId;
  sprintf(buff, "%d", iRecordId);
  strId = buff;

  return DeleteRecord(strId);
}

void PVRIptvData::SetPlaying(bool playing)
{
  m_bIsPlaying = playing;
}

string PVRIptvData::GetEventUrl(int channelId, time_t iStart, time_t iEnd)
{
  PVRIptvChannel *pChannel = FindChannel(channelId);

  if (pChannel == NULL)
  {
    return "";
  }

  string timeShift = m_manager.getEventTimeshift(FindTvShowId(*pChannel, iStart, iEnd));

  Json::Reader reader;
  Json::Value root;

  if (!reader.parse(timeShift, root))
  {
    XBMC->Log(LOG_NOTICE, "Cannot parse event.");
    return "";
  }

  return root.get("url", "").asString();
}

string PVRIptvData::GetRecordingUrl(const string &strRecordingId)
{
  return m_manager.getRecordingUrl(strRecordingId);
}

PVR_ERROR PVRIptvData::CanPlayEvent(const EPG_TAG *epgTag, bool *bCanPlay)
{
  string availability = GetAvailability(epgTag);

  time_t t = time(NULL);

  if (t == -1)
  {
    return PVR_ERROR_FAILED;
  }

  *bCanPlay = (availability == "pvr" || availability == "timeshift") && epgTag->startTime < t;
  return availability.empty() ? PVR_ERROR_FAILED : PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::CanRecordEvent(const EPG_TAG *epgTag, bool *bCanRecord)
{
  string availability = GetAvailability(epgTag);

  *bCanRecord = availability == "pvrAllowed" || availability == "timeshift";
  return availability.empty() ? PVR_ERROR_FAILED : PVR_ERROR_NO_ERROR;
}
