////////////////////////////////////////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2007-2008 Laurent Gomila (laurent.gom@gmail.com)
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it freely,
// subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented;
//    you must not claim that you wrote the original software.
//    If you use this software in a product, an acknowledgment
//    in the product documentation would be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such,
//    and must not be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////
#include <SFML/Audio/SoundStream.hpp>
#include <SFML/Audio/SoundBuffer.hpp>
#include <SFML/Audio/AudioDevice.hpp>
#include <SFML/Audio/OpenAL.hpp>
#include <SFML/System/Sleep.hpp>


namespace sf
{
////////////////////////////////////////////////////////////
/// Default constructor
////////////////////////////////////////////////////////////
SoundStream::SoundStream() :
myIsStreaming  (false),
myChannelsCount(0),
mySampleRate   (0),
myFormat       (0)
{

}


////////////////////////////////////////////////////////////
/// Virtual destructor
////////////////////////////////////////////////////////////
SoundStream::~SoundStream()
{
    // Stop the sound if it was playing
    Stop();
}


////////////////////////////////////////////////////////////
/// Set the audio stream parameters, you must call it before Play()
////////////////////////////////////////////////////////////
void SoundStream::Initialize(unsigned int ChannelsCount, unsigned int SampleRate)
{
    myChannelsCount = ChannelsCount;
    mySampleRate    = SampleRate;

    // Deduce the format from the number of channels
    myFormat = priv::AudioDevice::GetInstance().GetFormatFromChannelsCount(ChannelsCount);

    // Check if the format is valid
    if (myFormat == 0)
    {
        myChannelsCount = 0;
        mySampleRate    = 0;
        std::cerr << "Unsupported number of channels (" << myChannelsCount << ")" << std::endl;
    }
}


////////////////////////////////////////////////////////////
/// Start playing the audio stream
////////////////////////////////////////////////////////////
void SoundStream::Play()
{
    // Check if the sound parameters have been set
    if (myFormat == 0)
    {
        std::cerr << "Failed to play audio stream : sound parameters have not been initialized (call Initialize first)" << std::endl;
        return;
    }

    // If the sound is already playing (probably paused), just resume it
    if (myIsStreaming)
    {
        Sound::Play();
        return;
    }

    // Notify the derived class
    if (OnStart())
    {
        // Start updating the stream in a separate thread to avoid blocking the application
        myIsStreaming = true;
        Launch();
    }
}


////////////////////////////////////////////////////////////
/// Stop playing the audio stream
////////////////////////////////////////////////////////////
void SoundStream::Stop()
{
    // Wait for the thread to terminate
    myIsStreaming = false;
    Wait();
}


////////////////////////////////////////////////////////////
/// Return the number of channels (1 = mono, 2 = stereo, ...)
////////////////////////////////////////////////////////////
unsigned int SoundStream::GetChannelsCount() const
{
    return myChannelsCount;
}


////////////////////////////////////////////////////////////
/// Get the sound frequency (sample rate)
////////////////////////////////////////////////////////////
unsigned int SoundStream::GetSampleRate() const
{
    return mySampleRate;
}


////////////////////////////////////////////////////////////
/// Get the status of the sound (stopped, paused, playing)
////////////////////////////////////////////////////////////
Sound::Status SoundStream::GetStatus() const
{
    Status Status = Sound::GetStatus();

    // To compensate for the lag between Play() and alSourcePlay()
    if ((Status == Stopped) && myIsStreaming)
        Status = Playing;

    return Status;
}


////////////////////////////////////////////////////////////
/// /see sfThread::Run
////////////////////////////////////////////////////////////
void SoundStream::Run()
{
    bool RequestStop = false;

    // Enqueue the buffers
    ALCheck(alGenBuffers(BuffersCount, myBuffers));
    for (int i = 0; i < BuffersCount; ++i)
    {
        // Acquire audio data
        Chunk Data = {NULL, 0};
        if (OnGetData(Data))
        {
            // Create and fill a buffer, and push it to the queue
            if (Data.Samples && Data.NbSamples)
            {
                // Fill the buffer
                ALsizei Size = static_cast<ALsizei>(Data.NbSamples) * sizeof(Int16);
                ALCheck(alBufferData(myBuffers[i], myFormat, Data.Samples, Size, mySampleRate));

                // Push it into the sound queue
                ALCheck(alSourceQueueBuffers(Sound::mySource, 1, &myBuffers[i]));
            }
        }
        else
        {
            RequestStop = true;
            break;
        }
    }

    // Play the sound
    Sound::Play();

    while (myIsStreaming)
    {
        // The stream has been interrupted !
        if (Sound::GetStatus() == Stopped)
        {
            // User requested to stop : finish the streaming loop
            if (RequestStop)
            {
                myIsStreaming = false;
                break;
            }
            else
            {
                // Streaming is not completed : restart the sound
                Sound::Play();
            }
        }

        // Get the number of buffers that have been processed (ie. ready for reuse)
        ALint NbProcessed;
        ALCheck(alGetSourcei(Sound::mySource, AL_BUFFERS_PROCESSED, &NbProcessed));

        while (NbProcessed-- && !RequestStop)
        {
            // Pop the first unused buffer from the queue
            ALuint Buffer;
            ALCheck(alSourceUnqueueBuffers(Sound::mySource, 1, &Buffer));

            // Acquire new data
            Chunk Data = {NULL, 0};
            if (!OnGetData(Data))
                RequestStop = true;

            // Fill the buffer with the new data, and push it back to the queue
            if (Data.Samples && Data.NbSamples)
            {
                // Fill the buffer
                ALsizei Size = static_cast<ALsizei>(Data.NbSamples) * sizeof(Int16);
                ALCheck(alBufferData(Buffer, myFormat, Data.Samples, Size, mySampleRate));

                // Push it into the sound queue
                ALCheck(alSourceQueueBuffers(Sound::mySource, 1, &Buffer));
            }
        }

        // Leave some time for the other threads
        if (!RequestStop)
            Sleep(0.1f);
    }

    // Streaming finished : clean up everything
    CleanUp();
}


////////////////////////////////////////////////////////////
/// Clean up stream internal resources
////////////////////////////////////////////////////////////
void SoundStream::CleanUp()
{
    // Stop the playback
    Sound::Stop();

    // Unqueue any buffer left in the queue
    ALint  NbQueued;
    ALuint Buffer;
    ALCheck(alGetSourcei(Sound::mySource, AL_BUFFERS_QUEUED, &NbQueued));
    for (ALint i = 0; i < NbQueued; ++i)
        ALCheck(alSourceUnqueueBuffers(Sound::mySource, 1, &Buffer));

    // Delete the buffers
    ALCheck(alDeleteBuffers(BuffersCount, myBuffers));
}


////////////////////////////////////////////////////////////
/// Called when the sound restarts
////////////////////////////////////////////////////////////
bool SoundStream::OnStart()
{
    // Does nothing by default

    return true;
}

} // namespace sf
