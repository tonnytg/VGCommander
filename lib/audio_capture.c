#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>

#define kNumberBuffers 3

typedef struct {
    AudioQueueRef queue;
    AudioQueueBufferRef buffers[kNumberBuffers];
    AudioStreamBasicDescription dataFormat;
    int bufferByteSize;
    int recording;
    FILE *audioFile;
} AQRecorderState;

void HandleInputBuffer(
    void *aqData,
    AudioQueueRef inAQ,
    AudioQueueBufferRef inBuffer,
    const AudioTimeStamp *inStartTime,
    UInt32 inNumPackets,
    const AudioStreamPacketDescription *inPacketDesc
) {
    AQRecorderState *pAqData = (AQRecorderState *) aqData;
    if (inNumPackets > 0) {
        fwrite(inBuffer->mAudioData, inBuffer->mAudioDataByteSize, 1, pAqData->audioFile);
        printf("Written %u bytes to file\n", inBuffer->mAudioDataByteSize);
    }

    if (pAqData->recording) {
        AudioQueueEnqueueBuffer(pAqData->queue, inBuffer, 0, NULL);
    }
}

void SetupAudioFormat(AudioStreamBasicDescription *format) {
    format->mSampleRate = 44100.0;
    format->mFormatID = kAudioFormatLinearPCM;
    format->mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format->mFramesPerPacket = 1;
    format->mChannelsPerFrame = 1;
    format->mBitsPerChannel = 16;
    format->mBytesPerPacket = 2;
    format->mBytesPerFrame = 2;
}

void StartRecording(AQRecorderState *pAqData) {
    pAqData->recording = 1;
    OSStatus status = AudioQueueStart(pAqData->queue, NULL);
    if (status != noErr) {
        fprintf(stderr, "Error starting audio queue: %d\n", status);
    }
}

void StopRecording(AQRecorderState *pAqData) {
    pAqData->recording = 0;
    OSStatus status = AudioQueueStop(pAqData->queue, true);
    if (status != noErr) {
        fprintf(stderr, "Error stopping audio queue: %d\n", status);
    }
}

void InitializeRecorder(AQRecorderState *pAqData) {
    SetupAudioFormat(&pAqData->dataFormat);

    OSStatus status = AudioQueueNewInput(
        &pAqData->dataFormat,
        HandleInputBuffer,
        pAqData,
        NULL,
        kCFRunLoopCommonModes,
        0,
        &pAqData->queue
    );

    if (status != noErr) {
        fprintf(stderr, "Error creating audio queue: %d\n", status);
        return;
    }

    for (int i = 0; i < kNumberBuffers; ++i) {
        status = AudioQueueAllocateBuffer(pAqData->queue, 2048, &pAqData->buffers[i]);
        if (status != noErr) {
            fprintf(stderr, "Error allocating buffer %d: %d\n", i, status);
            return;
        }
        status = AudioQueueEnqueueBuffer(pAqData->queue, pAqData->buffers[i], 0, NULL);
        if (status != noErr) {
            fprintf(stderr, "Error enqueuing buffer %d: %d\n", i, status);
            return;
        }
    }
}

void WriteWavHeader(FILE *file, AudioStreamBasicDescription *format, int totalDataLength) {
    int32_t chunkSize = totalDataLength + 36;
    int16_t audioFormat = 1;
    int16_t numChannels = format->mChannelsPerFrame;
    int32_t sampleRate = format->mSampleRate;
    int16_t bitsPerSample = format->mBitsPerChannel;
    int16_t blockAlign = numChannels * bitsPerSample / 8;
    int32_t byteRate = sampleRate * blockAlign;

    fwrite("RIFF", 1, 4, file);
    fwrite(&chunkSize, 4, 1, file);
    fwrite("WAVE", 1, 4, file);

    fwrite("fmt ", 1, 4, file);
    int32_t subchunk1Size = 16;
    fwrite(&subchunk1Size, 4, 1, file);
    fwrite(&audioFormat, 2, 1, file);
    fwrite(&numChannels, 2, 1, file);
    fwrite(&sampleRate, 4, 1, file);
    fwrite(&byteRate, 4, 1, file);
    fwrite(&blockAlign, 2, 1, file);
    fwrite(&bitsPerSample, 2, 1, file);

    fwrite("data", 1, 4, file);
    int32_t subchunk2Size = totalDataLength;
    fwrite(&subchunk2Size, 4, 1, file);
}

int main() {
    AQRecorderState recorder;
    recorder.audioFile = fopen("output.wav", "wb");
    if (!recorder.audioFile) {
        fprintf(stderr, "Failed to open output file\n");
        return 1;
    }

    InitializeRecorder(&recorder);

    // Write placeholder for header, will update later
    fseek(recorder.audioFile, 44, SEEK_SET);

    StartRecording(&recorder);

    printf("Recording... Press Enter to stop.\n");
    getchar();

    StopRecording(&recorder);

    // Update header with final file size
    fseek(recorder.audioFile, 0, SEEK_END);
    int totalDataLength = ftell(recorder.audioFile) - 44;
    fseek(recorder.audioFile, 0, SEEK_SET);
    WriteWavHeader(recorder.audioFile, &recorder.dataFormat, totalDataLength);

    fclose(recorder.audioFile);
    AudioQueueDispose(recorder.queue, true);

    return 0;
}

