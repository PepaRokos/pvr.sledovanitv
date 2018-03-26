#pragma once
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

#include <vector>
#include "p8-platform/util/StdString.h"
#include "client.h"
#include "p8-platform/threads/threads.h"
#include "apimanager.h"

struct PVRIptvEpgEntry
{
  int         iBroadcastId;
  int         iChannelId;
  int         iGenreType;
  int         iGenreSubType;
  time_t      startTime;
  time_t      endTime;
  std::string strTitle;
  std::string strPlotOutline;
  std::string strPlot;
  std::string strIconPath;
  std::string strGenreString;
};

struct PVRIptvEpgChannel
{
  std::string                  strId;
  std::string                  strName;
  std::vector<PVRIptvEpgEntry> epg;
};

struct PVRIptvChannel
{
  bool        bRadio;
  int         iUniqueId;
  int         iChannelNumber;
  int         iEncryptionSystem;
  int         iTvgShift;
  std::string strChannelName;
  std::string strLogoPath;
  std::string strStreamURL;
  std::string strTvgId;
  std::string strTvgName;
  std::string strTvgLogo;
  std::map<std::string, std::string> properties;
};

struct PVRIptvChannelGroup
{
  bool              bRadio;
  int               iGroupId;
  std::string       strGroupName;
  std::vector<int>  members;
};

struct PVRIptvRecording
{
  std::string		strRecordId;
  std::string		strTitle;
  std::string		strStreamUrl;
  std::string		strPlotOutline;
  std::string		strPlot;
  std::string		strChannelName;
  time_t		startTime;
  int			duration;
};

struct PVRIptvTimer
{
  unsigned int    iClientIndex;
  int             iClientChannelUid;
  time_t          startTime;
  time_t          endTime;
  PVR_TIMER_STATE state;                                     /*!< @brief (required) the state of this timer */
  std::string     strTitle;
  std::string     strSummary;
  int             iLifetime;
  bool            bIsRepeating;
  time_t          firstDay;
  int             iWeekdays;
  int             iEpgUid;
  unsigned int    iMarginStart;
  unsigned int    iMarginEnd;
  int             iGenreType;
  int             iGenreSubType;
};

class PVRIptvData : public P8PLATFORM::CThread
{
public:
  PVRIptvData(void);
  virtual ~PVRIptvData(void);

  int       GetChannelsAmount(void);
  PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio);
  bool      GetChannel(const PVR_CHANNEL &channel, PVRIptvChannel &myChannel);
  bool      GetRecording(const PVR_RECORDING &recording, PVRIptvRecording &myRecording);
  int       GetChannelGroupsAmount(void);
  PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio);
  PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group);
  PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd);
  int       GetRecordingsAmount();
  PVR_ERROR GetRecordings(ADDON_HANDLE handle);
  int       GetTimersAmount();
  PVR_ERROR GetTimers(ADDON_HANDLE handle);
  PVR_ERROR AddTimer(const PVR_TIMER &timer);
  PVR_ERROR DeleteRecord(const std::string &strRecordId);
  PVR_ERROR DeleteRecord(int iRecordId);
  void      SetPlaying(bool playing);
  std::string GetEventUrl(int channelId, time_t iStart, time_t iEnd);
  std::string GetRecordingUrl(const std::string &strRecordingId);

protected:
  bool                 LoadPlayList(void);
  bool                 LoadEPG(time_t iStart, time_t iEnd);
  bool                 LoadRecordings();
  int                  GetFileContents(CStdString& url, std::string &strContent);
  PVRIptvChannel      *FindChannel(const std::string &strId, const std::string &strName);
  PVRIptvChannel      *FindChannel(int iChannelUid);
  PVRIptvChannelGroup *FindGroup(const std::string &strName);
  PVRIptvEpgChannel   *FindEpg(const std::string &strId);
  std::string          FindTvShowId(const PVRIptvChannel &channel, time_t iStart, time_t iEnd);
  PVRIptvEpgChannel   *FindEpgForChannel(PVRIptvChannel &channel);
  int                  ParseDateTime(std::string strDate);
  int                  GetCachedFileContents(const std::string &strCachedName, const std::string &strFilePath,
                                             std::string &strContent, const bool bUseCache = false);
  int                  GetChannelId(const char * strChannelName, const char * strStreamUrl);

protected:
  virtual void *Process(void) override;

private:
  bool                              m_bEGPLoaded;
  bool                              m_bKeepAlive;
  bool                              m_bIsPlaying;
  int                               m_iLastStart;
  int                               m_iLastEnd;
  std::vector<PVRIptvChannelGroup>  m_groups;
  std::vector<PVRIptvChannel>       m_channels;
  std::vector<PVRIptvEpgChannel>    m_epg;
  std::vector<PVRIptvRecording>     m_recordings;
  std::vector<PVRIptvTimer>         m_timers;
  P8PLATFORM::CMutex                m_mutex;
  P8PLATFORM::CMutex                m_mutexLogin;
  void ThreadSafeLogin();

  ApiManager                        m_manager;
};
