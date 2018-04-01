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

#include <json/json.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif
#include <fstream>
#include <iostream>

#include "p8-platform/os.h"

#include "client.h"
#include "apimanager.h"
#include <ctime>

using namespace ADDON;

const std::string ApiManager::API_URL = "http://sledovanitv.cz/api/";
const std::string ApiManager::LOG_PREFIX = "sledovanitv.cz - ";
const std::string ApiManager::PAIR_FILE = "pairinfo";

/* Converts a hex character to its integer value */
char from_hex(char ch)
{
  return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/* Converts an integer value to its hex character*/
char to_hex(char code)
{
  static char hex[] = "0123456789abcdef";
  return hex[code & 15];
}

char *url_encode(const char *str)
{
  char *pstr = (char*) str, *buf = (char *)malloc(strlen(str) * 3 + 1), *pbuf = buf;
  while (*pstr)
{
  if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~')
    *pbuf++ = *pstr;
  else if (*pstr == ' ')
    *pbuf++ = '+';
  else
    *pbuf++ = '%', *pbuf++ = to_hex(*pstr >> 4), *pbuf++ = to_hex(*pstr & 15);
  pstr++;
  }
  *pbuf = '\0';
  return buf;
}

ApiManager::ApiManager()
{
  XBMC->Log(LOG_NOTICE, "Loading ApiManager");
}

std::string ApiManager::apiCall(const std::string &function, ApiParamMap paramsMap)
{
  std::string url = API_URL + function + "?" + buildQueryString(paramsMap);
  std::string response;

  void *fh = XBMC->OpenFile(url.c_str(), 0);
  if (fh)
  {
    char buffer[1024];
    while (int bytesRead = XBMC->ReadFile(fh, buffer, 1024))
      response.append(buffer, bytesRead);
    XBMC->CloseFile(fh);
  }
  else
  {
    XBMC->Log(LOG_ERROR, "Cannot open url");
  }

  return response;
}

bool ApiManager::isSuccess(const std::string &response)
{
  Json::Reader reader;
  Json::Value root;

  if (reader.parse(response, root))
  {
    return root.get("status", 0).asInt() == 1;
    }
}

int ApiManager::getFileContents(const std::string &strUrl, std::string &strContent)
{
  strContent.clear();
  void* fileHandle = XBMC->OpenFile(strUrl.c_str(), 0);
  if (fileHandle)
  {
    char buffer[1024];
    while (int bytesRead = XBMC->ReadFile(fileHandle, buffer, 1024))
      strContent.append(buffer, bytesRead);
    XBMC->CloseFile(fileHandle);
  }

  return strContent.length();
}

int ApiManager::getCachedFileContents(const std::string &strCachedName, const std::string &strOrigFilePath, std::string &strContents, const bool bUseCache)
{
  bool bNeedReload = false;
  std::string strCachedPath = GetUserFilePath(strCachedName);
  std::string strFilePath = strOrigFilePath;

  // check cached file is exists
  if (bUseCache && XBMC->FileExists(strCachedPath.c_str(), false))
  {
    struct __stat64 statCached;
    struct __stat64 statOrig;

    XBMC->StatFile(strCachedPath.c_str(), &statCached);
    XBMC->StatFile(strFilePath.c_str(), &statOrig);

    bNeedReload = statCached.st_mtime < statOrig.st_mtime || statOrig.st_mtime == 0;
  }
  else
  {
    bNeedReload = true;
  }

  if (bNeedReload)
  {
    getFileContents(strFilePath, strContents);

    // write to cache
    if (bUseCache && strContents.length() > 0)
    {
      void* fileHandle = XBMC->OpenFileForWrite(strCachedPath.c_str(), true);
      if (fileHandle)
      {
        XBMC->WriteFile(fileHandle, strContents.c_str(), strContents.length());
        XBMC->CloseFile(fileHandle);
      }
    }
    return strContents.length();
  }

  return getFileContents(strCachedPath, strContents);
}

std::string ApiManager::formatTime(time_t time)
{
  char bufDateTime[256];
  std::strftime(bufDateTime, sizeof(bufDateTime), "%Y-%m-%d %H:%M", std::localtime(&time));
  return bufDateTime;
}

std::string ApiManager::toString(int num)
{
  char bufNumber[256];
  sprintf(bufNumber, "%d", num);
  return bufNumber;
}

bool ApiManager::pairDevice()
{
  std::string pairJson = readPairFile();

  if (pairJson.empty())
  {
    ApiParamMap params;

#ifndef _WIN32
    char hostName[256];
    gethostname(hostName, 256);

    std::string macAddr;
    std::ifstream ifs("/sys/class/net/eth0/address");
    if (ifs.is_open())
    {
      std::getline(ifs, macAddr);
    }
#else
    char *hostName = "Kodi Win32";
    std::string macAddr = "11:22:33:44";
#endif

    params["username"] = g_strUserName;
    params["password"] = g_strPassword;
    params["type"] = "xbmc";
    params["product"] = hostName;
    params["serial"] = macAddr;

    pairJson = apiCall("create-pairing", params);
  }

  Json::Reader reader;
  Json::Value root;

  if (reader.parse(pairJson, root))
  {
    int devId = root.get("deviceId", 0).asInt();
    std::string passwd = root.get("password", "").asString();

    char buf[256];
    sprintf(buf, "%d", devId);
    m_deviceId = buf;
    m_password = passwd;

    XBMC->Log(LOG_DEBUG, "Device ID: %d, Password: %s", devId, passwd.c_str());

    if (!m_deviceId.empty() && !m_password.empty())
    {
      createPairFile(pairJson);
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    XBMC->Log(LOG_ERROR, "Error parsing pairing response. Response is: %s, reader error: %s", pairJson.c_str(), reader.getFormatedErrorMessages().c_str());
  }

  return false;
}

bool ApiManager::login()
{
  if (m_deviceId.empty() && m_password.empty())
  {
    if (!pairDevice())
    {
      XBMC->Log(LOG_ERROR, "Cannot pair device");
      return false;
    }
  }

  ApiParamMap param;
  param["deviceId"] = m_deviceId;
  param["password"] = m_password;

  std::string resp = apiCall("device-login", param);

  Json::Reader reader;
  Json::Value root;

  if (reader.parse(resp, root))
  {
    m_sessionId = root.get("PHPSESSID", "").asString();

    if (m_sessionId.empty())
    {
      XBMC->Log(LOG_ERROR, "Cannot perform device login");
    }
    else
    {
      XBMC->Log(LOG_INFO, "Device logged in. Session ID: %s", m_sessionId.c_str());
    }
  }

  return !m_sessionId.empty();
}

std::string ApiManager::getPlaylist()
{
  ApiParamMap params;
  params["format"] = "androidtv";
  return apiCall("playlist", params);
}

std::string ApiManager::getStreamQualities()
{
    return apiCall("get-stream-qualities", ApiParamMap());
}

std::string ApiManager::getEpg(time_t start, int duration)
{
  ApiParamMap params;

  params["detail"] = "1";

  if (start != 0)
    params["time"] = formatTime(start);

  if (duration != 0)
    params["duration"] = toString((duration > MAX_EPG_DURATION ? MAX_EPG_DURATION : duration));

  return apiCall("epg", params);
}

std::string ApiManager::getPvr()
{
  return apiCall("get-pvr", ApiParamMap());
}

std::string ApiManager::getRecordingUrl(const std::string &recId)
{
  ApiParamMap param;
  param["recordId"] = recId;
  param["format"] = "m3u8";

  std::string resp = apiCall("record-timeshift", param);

  Json::Reader reader;
  Json::Value root;

  if (reader.parse(resp, root))
  {
    return root.get("url", "").asString();
  }

  return "";
}

bool ApiManager::addTimer(const std::string &eventId)
{
  ApiParamMap param;
  param["eventId"] = eventId;

  return isSuccess(apiCall("record-event", param));
}

std::string ApiManager::getEventId(const std::string &channel, time_t start, time_t end)
{
  char bufStart[256];
  std::string strStart;
  char bufDuration[10];
  std::string strDuration;
  ApiParamMap param;

  start += 60;
  std::strftime(bufStart, sizeof(bufStart), "%Y-%m-%d %H:%M", std::localtime(&start));
  sprintf(bufDuration, "%d", ((end - start) - 60)/60);
  strStart = bufStart;
  strDuration = bufDuration;

  param["time"] = strStart;
  param["duration"] = strDuration;
  param["channels"] = channel;

  return apiCall("epg", param);
}

std::string ApiManager::getEventTimeshift(const std::string &eventId)
{
  ApiParamMap param;
  param["eventId"] = eventId;
  param["format"] = "m3u8";

  return apiCall("event-timeshift", param);
}

bool ApiManager::deleteRecord(const std::string &recId)
{
  ApiParamMap param;
  param["recordId"] = recId;

  return isSuccess(apiCall("delete-record", param));
}

bool ApiManager::keepAlive()
{
    ApiParamMap param;
    return isSuccess(apiCall("keepalive", param));
}

std::string ApiManager::urlEncode(const std::string &str)
{
  std::string strOut;
  strOut.append(url_encode(str.c_str()));

  return strOut;
}

std::string ApiManager::buildQueryString(ApiParamMap paramMap)
{
  std::string strOut = "PHPSESSID=" + m_sessionId;

  for (ApiParamMap::iterator i = paramMap.begin(); i != paramMap.end(); i++)
  {
    std::string key(i->first);

    if (!strOut.empty())
    {
      strOut += "&";
    }

    strOut += key + "=" + urlEncode(paramMap.at(key));
  }

  return strOut;
}

std::string ApiManager::readPairFile()
{
  std::string url = GetUserFilePath(PAIR_FILE);
  std::string strContent;

  XBMC->Log(LOG_DEBUG, "Openning file %s", url.c_str());

  void* fileHandle = XBMC->OpenFile(url.c_str(), 0);
  if (fileHandle)
  {
    char buffer[1024];
    while (int bytesRead = XBMC->ReadFile(fileHandle, buffer, 1024))
    strContent.append(buffer, bytesRead);
    XBMC->CloseFile(fileHandle);
  }

  return strContent;
}

void ApiManager::createPairFile(const std::string &content)
{
  std::string url = GetUserFilePath(PAIR_FILE);

  void *fileHandle = XBMC->OpenFileForWrite(url.c_str(), false);
  if (fileHandle)
  {
    XBMC->WriteFile(fileHandle, content.c_str(), content.length());
  }
}
