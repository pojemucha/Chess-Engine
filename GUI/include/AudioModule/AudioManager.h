#pragma once

#include <SFML/Audio.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>

class AudioManager {
private:
    enum class SoundState { Playing, FadingOut }; // enumerator to check Sound state; Playing - active Sound; FadingOut - unactive Sound which will be deleted 
    
    struct SoundInstance { // structure to increase sf::Sound properties
        sf::Sound sound;
        int priority; // to check and filter Sounds by its priority
        SoundState state = SoundState::Playing; 

        SoundInstance(const sf::SoundBuffer& buffer, int p) 
            : priority(p) {
                sound.setBuffer(buffer);
            }
    };

    inline static float                                 sfxVolume           = 50.0f; // Sound effects volume
    inline static bool                                  sfxVolumeChanged    = false; // Checks if sound effects volume has changed
    inline static float                                 musicVolume         = 50.0f; // Music volume
    inline static bool                                  musicVolumeChanged  = false; // Checks if music volume has changed
    inline static unsigned int                          soundLimit          = 100;   // Limit on the number of sounds
    std::vector<SoundInstance>                          activeSounds;                // Conteiner for all active sounds
    sf::Music                                           currentMusic;                // Variable for current Music
    std::unordered_map<std::string, sf::SoundBuffer>    cachedSounds;                // Container for sound buffers
    
    
    sf::SoundBuffer& getBuffer(const std::string& filename) { // Resource cache, returns sound buffer by filename path
        auto it = cachedSounds.find(filename);
        if(it != cachedSounds.end()) 
            return it->second;
        
        sf::SoundBuffer tempBuffer;
        if(tempBuffer.loadFromFile(filename)) {
            auto [it, inserted] = cachedSounds.emplace(filename, tempBuffer);
            return it->second;
        }
        else {
            std::cerr << "Failed to load sound: " << filename << '\n';
            throw std::runtime_error("Audio load failed");
        }
    }
    static bool cleanSounds(const sf::Sound& sound) { 
        return (sound.getStatus() == sf::Sound::Status::Stopped); 
    }
public:
    void playSound(const std::string& filename, const int priority) { // Plays the sound 
        if (activeSounds.size() < soundLimit - 10) { // Reserve buffer for emergency sounds
            activeSounds.emplace_back(getBuffer(filename), priority);
            activeSounds.back().sound.setVolume(sfxVolume);
            activeSounds.back().sound.play();
        }
        else if (activeSounds.size() < soundLimit) {
            std::sort(activeSounds.begin(), activeSounds.end()
                , [](const SoundInstance& a, const SoundInstance& b) 
                { return (a.state != b.state ? a.state != SoundState::FadingOut : a.priority < b.priority); } ); // sort algorithm depending on sounds state and their priority
            if (priority < activeSounds.front().priority) return;
            else {
                activeSounds.front().state = SoundState::FadingOut;
                activeSounds.emplace_back(getBuffer(filename), priority);
                activeSounds.back().sound.setVolume(sfxVolume);
                activeSounds.back().sound.play();
            }
        }
        else {
            std::sort(activeSounds.begin(), activeSounds.end()
                , [](const SoundInstance& a, const SoundInstance& b) 
                { return (a.state != b.state ? a.state == SoundState::FadingOut : a.priority < b.priority); } );
            if (priority < activeSounds.front().priority) return;
            else {
                activeSounds.erase(activeSounds.begin());
                activeSounds.emplace_back(getBuffer(filename), priority);
                activeSounds.back().sound.setVolume(sfxVolume);
                activeSounds.back().sound.play();
            }
        }
    }
    void playMusic(const std::string& filename) { // plays Music
        currentMusic.stop();
        if (currentMusic.openFromFile(filename)) {
            currentMusic.setLoop(true);
            currentMusic.setVolume(musicVolume);
            currentMusic.play();
        }
        else {
            std::cerr << "Failed to load music: " << filename << '\n';
            throw std::runtime_error("Audio load failed");
        }
    }
    void update(const float deltaTime) { // function to update state and volume of Sounds and Music
        auto i = activeSounds.begin();
        while(i != activeSounds.end()) {
            if (i->state == SoundState::FadingOut) { // if sound is fading out, decrising its volume until we can stop it
                float fadeSpeed = 100.0f;
                float newVolume = i->sound.getVolume() - fadeSpeed * deltaTime;
                if (newVolume <= 0) {
                    i->sound.stop();
                }
                else {
                    i->sound.setVolume(newVolume);
                }
            }
            if (cleanSounds(i->sound)) { // erase suitable sounds 
                i = activeSounds.erase(i);
            } 
            else {
                if (sfxVolumeChanged && i->state != SoundState::FadingOut) // changing the volume of the Sounds
                    i->sound.setVolume(sfxVolume);
                i++;
            }
        }
        
        if (musicVolumeChanged) {
            currentMusic.setVolume(musicVolume);
            musicVolumeChanged = false;
        }
        sfxVolumeChanged = false;
    }

    float getSFXVolume() const {
        return sfxVolume;
    }
    void setSFXVolume(const float volume) {
        if (sfxVolumeChanged = sfxVolume != volume)
            sfxVolume = std::clamp(volume, 0.f, 100.f);
    }
    float getMusicVolume() const {
        return musicVolume;
    }
    void setMusicVolume(const float volume) {
        if (musicVolumeChanged = musicVolume != volume)
            musicVolume = std::clamp(volume, 0.f, 100.f);
    }
};