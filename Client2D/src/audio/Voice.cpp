#include "audio/Voice.h"
#include "audio/Sfx.h"

#include "raylib.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace voice
{
	namespace
	{
		std::unordered_map<std::string, Sound> g_lines;
		const Sound* g_current = nullptr;
		int g_currentPriority = 0;
	}

	void Init(const std::string& dir)
	{
		Shutdown();
		if (!sfx::Ready() || !IsAudioDeviceReady())
			return;
		const std::string list = dir + "/voice.txt";
		if (!FileExists(list.c_str()))
			return;
		char* text = LoadFileText(list.c_str());
		if (!text)
			return;
		for (char* line = std::strtok(text, "\r\n"); line; line = std::strtok(nullptr, "\r\n"))
		{
			if (line[0] == '#') continue;
			const char* tab = std::strchr(line, '\t');
			const std::string key = tab ? std::string(line, static_cast<size_t>(tab - line)) : std::string(line);
			if (key.empty()) continue;
			const std::string path = dir + "/" + key + ".ogg";
			if (!FileExists(path.c_str())) continue;
			Sound s = LoadSound(path.c_str());
			if (s.frameCount > 0)
				g_lines[key] = s;
		}
		UnloadFileText(text);
		TraceLog(LOG_INFO, "VOICE: %d lines", static_cast<int>(g_lines.size()));
	}

	void Shutdown()
	{
		for (auto& [k, s] : g_lines)
			UnloadSound(s);
		g_lines.clear();
		g_current = nullptr;
	}

	bool Has(std::string_view key) { return g_lines.find(std::string(key)) != g_lines.end(); }

	void Stop()
	{
		if (g_current && IsSoundPlaying(*g_current))
			StopSound(*g_current);
		g_current = nullptr;
	}

	bool Play(std::string_view key, float volume, int priority)
	{
		auto it = g_lines.find(std::string(key));
		if (it == g_lines.end())
			return false;
		if (g_current && IsSoundPlaying(*g_current))
		{
			if (priority < g_currentPriority)
				return false;
			StopSound(*g_current);
		}
		Sound& s = it->second;
		SetSoundVolume(s, std::min(1.f, volume * sfx::MasterVolume() * 1.15f));
		SetSoundPitch(s, 1.f);
		PlaySound(s);
		g_current = &s;
		g_currentPriority = priority;
		return true;
	}
}
