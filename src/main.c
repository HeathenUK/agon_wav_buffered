#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <agon/vdp_vdu.h>
#include <agon/vdp_key.h>

#define DEFAULT_CHUNK_SIZE 8192

extern void fast_vdu(uint8_t *data, int len);

void write16bit(uint16_t w)
{
	putch(w & 0xFF); // write LSB
	putch(w >> 8);	 // write MSB	
}

void add_stream_to_buffer(uint16_t buffer_id, char* buffer_content, uint16_t buffer_size, bool quick) {	

	putch(23);
	putch(0);
	putch(0xA0);
	write16bit(buffer_id);
	putch(0);
	write16bit(buffer_size);
	
	if (quick) fast_vdu(buffer_content, buffer_size);
    else mos_puts(buffer_content, buffer_size, 0);

}

void sample_from_buffer(uint16_t buffer_id, uint8_t format) {

	putch(23);
	putch(0);
	putch(0x85);	
	putch(0);
	putch(5);
	putch(2);
	write16bit(buffer_id);
	putch(format);

}

void clear_buffer(uint16_t buffer_id) {
	
	putch(23);
	putch(0);
	putch(0xA0);
	write16bit(buffer_id);
	putch(2);	
	
}

void assign_sample_to_channel(uint16_t sample_id, uint8_t channel_id) {
	
	putch(23);
	putch(0);
	putch(0x85);
	putch(channel_id);
	putch(4);
	putch(8);
	write16bit(sample_id);
	
}

void enable_channel(uint8_t channel) {

	//VDU 23, 0, &85, channel, 8
	putch(23);
	putch(0);
	putch(0x85);
	putch(channel);
	putch(8);

}

void play_sample(uint16_t sample_id, uint8_t channel, uint8_t volume, uint16_t duration) {

	assign_sample_to_channel(sample_id, channel);

	enable_channel(channel);

	putch(23);
	putch(0);
	putch(0x85);
	putch(channel);
	putch(0);
	putch(volume);
	write16bit(100);
	write16bit(duration);

}

int8_t getByte(uint32_t bitmask) {

    if (bitmask & 0xFF) {
        return 0;
    }
    else if ((bitmask >> 8) & 0xFF) {
        return 1;
    }
	else if ((bitmask >> 16) & 0xFF) {
        return 2;
    }
    else if ((bitmask >> 24) & 0xFF) {
        return 3;
    }

    return -1;
}

void print_bin(void* value, size_t size) {
    
	int i, j;
	unsigned char* bytes = (unsigned char*)value;
	
	if (size == 0) {
        printf("Error: Invalid size\n");
        return;
    }

    for (i = size - 1; i >= 0; i--) {
        for (j = 7; j >= 0; j--) {
            printf("%d", (bytes[i] >> j) & 1);
        }
    }
}

uint16_t strtou16(const char *str) {
    uint16_t result = 0;
    const uint16_t maxDiv10 = 6553;  // 65535 / 10
    const uint16_t maxMod10 = 5;     // 65535 % 10

    while (*str == ' ' || *str == '\t' || *str == '\n') {
        str++;
    }

    while (*str >= '0' && *str <= '9') {
        uint16_t digit = *str - '0';
        if (result > maxDiv10 || (result == maxDiv10 && digit > maxMod10)) {
            return 65535;
        }
        result = result * 10 + digit;
        str++;
    }

    return result;
}

uint8_t strtou8(const char *str) {
    uint8_t result = 0;
    const uint8_t maxDiv10 = 255 / 10;
    const uint8_t maxMod10 = 255 % 10;

    while (*str == ' ' || *str == '\t' || *str == '\n') {
        str++;
    }

    while (*str >= '0' && *str <= '9') {
        uint8_t digit = *str - '0';
        if (result > maxDiv10 || (result == maxDiv10 && digit > maxMod10)) {
            return 255;
        }
        result = result * 10 + digit;
        str++;
    }

    return result;
}

uint24_t strtou24(const char *str) {
    uint32_t result = 0;
    const uint32_t maxDiv10 = 1677721;
    const uint32_t maxMod10 = 5;

    while (*str == ' ' || *str == '\t' || *str == '\n') {
        str++;
    }
	
    while (*str >= '0' && *str <= '9') {
        uint32_t digit = *str - '0';
        if (result > maxDiv10 || (result == maxDiv10 && digit > maxMod10)) {
            return 16777215;
        }
        result = result * 10 + digit;
        str++;
    }

    return result;
}

typedef struct {

	uint32_t dataOffset;
	uint32_t dataSize;
	uint32_t samples;
	uint16_t duration;
    uint8_t  code;

} xHeader;

typedef struct {
    char     chunkId[4];
    uint32_t chunkSize;
    char     format[4];
    char     subChunk1Id[4];
    uint32_t subChunk1Size;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
	xHeader  extra_header;

} WavHeader;

WavHeader parse_wav(FILE *file) {
    WavHeader header = {0};
	uint8_t dataFound = 0;
    
	fseek(file, 0, SEEK_SET);

    // Read WAV header (less the extra header fields)
	fread(&header, 1, sizeof(WavHeader) - sizeof(xHeader), file);

    // Check if it's a WAV file
    if (strncmp(header.chunkId, "RIFF", 4) != 0 || strncmp(header.format, "WAVE", 4) != 0) {
        header.extra_header.code = 1; // Not a WAV file
        return header;
    }

    // Check if it's PCM audio
    if (header.audioFormat != 1) {
        header.extra_header.code = 2; // Not PCM
        return header;
    }

    if (!(header.numChannels == 1 || header.numChannels == 2)) {
        header.extra_header.code = 4; // Sorry, I don't support 5.1 surround sound!
        return header;
    }

    // Skip to the data chunk, allowing for extra chunks
    char subChunkId[4];
    uint32_t subChunkSize;
    size_t readCount;
    do {
        readCount = fread(subChunkId, 1, 4, file);
        if (readCount < 4) break;

        readCount = fread(&subChunkSize, 4, 1, file);
        if (readCount < 1) break;

        if (strncmp(subChunkId, "data", 4) == 0) {
            header.extra_header.dataSize = subChunkSize;
			header.extra_header.dataOffset = ftell(file);
            dataFound = 1;
            break;
        }
        
        // Skip this chunk and go to the next one
        fseek(file, subChunkSize, SEEK_CUR);
    } while (!feof(file) && readCount > 0);

    if (!dataFound) {
        printf("Error: 'data' chunk not found.\n");
        header.extra_header.code = 3; // Error code for data chunk not found
    }	

    header.extra_header.duration = header.extra_header.dataSize / header.numChannels / (header.sampleRate / 1000);

    return header; // Success
}

uint8_t* convertStereoToMono(uint8_t *stereoBuffer, int numFrames) {

    uint8_t *monoBuffer = (uint8_t*) malloc(numFrames * sizeof(uint8_t));
    if (monoBuffer == NULL) {
        return NULL;
    }

    for (int i = 0; i < numFrames; ++i) {
        monoBuffer[i] = (stereoBuffer[2 * i] + stereoBuffer[2 * i + 1]) / 2;
    }

    return monoBuffer;
}

void stream_pcm(FILE *file, WavHeader *header, uint16_t sample_id, uint16_t chunk_size, uint8_t channel_id, uint8_t volume, uint16_t duration, bool quick) {

//stream_pcm(params.buffer = sample_id, params.channel, volume, duration);

    if (chunk_size == 0) chunk_size = DEFAULT_CHUNK_SIZE;

	uint24_t chunk;
    int24_t remaining_data;
	uint8_t *sample_buffer = (uint8_t *) malloc(chunk_size);
	//uint8_t sample_buffer[CHUNK_SIZE];

	if (sample_buffer == NULL) {

		printf("Failed to malloc");
		return;

	}
		
	remaining_data = header->extra_header.dataSize;
	fseek(file, header->extra_header.dataOffset, SEEK_CUR);

	clear_buffer(sample_id);

    if (remaining_data > chunk_size) {
        chunk = chunk_size;
    } else chunk = remaining_data;
    
    fread(sample_buffer, 1, chunk * (header->bitsPerSample / 8), file);

    if (header->numChannels == 1) add_stream_to_buffer(sample_id, sample_buffer, chunk, quick);
    else if (header->numChannels == 2) {
        int stereo_samples = chunk / 2;

        uint8_t *monoBuffer = convertStereoToMono(sample_buffer, stereo_samples);
        if (monoBuffer != NULL) {
            add_stream_to_buffer(sample_id, monoBuffer, stereo_samples, quick);
            free(monoBuffer);
        }
        else {
            printf("Memory allocation failed for mono buffer.\n");
        }
    }

    remaining_data -= chunk;

    sample_from_buffer(sample_id, 1);
    play_sample(sample_id, channel_id, volume, duration);
    	
	while (remaining_data > 0) {
		
		if (remaining_data > chunk_size) {
			chunk = chunk_size;
		} else chunk = remaining_data;
		
		fread(sample_buffer, 1, chunk * (header->bitsPerSample / 8), file);

        if (header->numChannels == 1) add_stream_to_buffer(sample_id, sample_buffer, chunk, quick);
        else if (header->numChannels == 2) {
            int stereo_samples = chunk / 2;

            uint8_t *monoBuffer = convertStereoToMono(sample_buffer, stereo_samples);
            if (monoBuffer != NULL) {
                add_stream_to_buffer(sample_id, monoBuffer, stereo_samples, quick);
                free(monoBuffer);
            }
            else {
                printf("Memory allocation failed for mono buffer.\n");
            }
        }

		remaining_data -= chunk;
	
	}

	free(sample_buffer);

}

void upload_pcm(FILE *file, WavHeader *header, uint16_t sample_id, uint16_t chunk_size, bool quick) {

    if (chunk_size == 0) chunk_size = DEFAULT_CHUNK_SIZE;

	uint24_t chunk;
    int24_t remaining_data;
	uint8_t *sample_buffer = (uint8_t *) malloc(chunk_size);
	//uint8_t sample_buffer[CHUNK_SIZE];

	if (sample_buffer == NULL) {

		printf("Failed to malloc");
		return;

	}
		
	remaining_data = header->extra_header.dataSize;
	fseek(file, header->extra_header.dataOffset, SEEK_CUR);

	clear_buffer(sample_id);
		
	while (remaining_data > 0) {
		
		if (remaining_data > chunk_size) {
			chunk = chunk_size;
		} else chunk = remaining_data;
		
		fread(sample_buffer, 1, chunk * (header->bitsPerSample / 8), file);

        if (header->numChannels == 1) add_stream_to_buffer(sample_id, sample_buffer, chunk, quick);
        else if (header->numChannels == 2) {
            int stereo_samples = chunk / 2;

            uint8_t *monoBuffer = convertStereoToMono(sample_buffer, stereo_samples);
            if (monoBuffer != NULL) {
                add_stream_to_buffer(sample_id, monoBuffer, stereo_samples, quick);
                free(monoBuffer);
            }
            else {
                printf("Memory allocation failed for mono buffer.\n");
            }
        }

		remaining_data -= chunk;
	
	}

	sample_from_buffer(sample_id, 1);
	free(sample_buffer);

}

void to_lowercase(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower(str[i]);
    }
}

typedef struct {
    char *file;
    uint16_t buffer;
    bool play;
    bool loop;
	uint8_t channel;
	bool info;
	uint8_t volume;
    uint8_t repeat;
    uint16_t chunk;
    bool stream;
    bool quick;
} cli;

typedef struct {
    char **keys;
    uint8_t num_keys;
    void *ptr;
    char type;  // 'i' for int, 'b' for bool, 'f' for flag, 's' for string
    bool is_set;  // To check if the parameter was set
} arg_map;

arg_map args[] = {
    { .keys = (char *[]){ "--file", "-f" },		.num_keys = 2, .ptr = NULL, .type = 's', .is_set = false },
    { .keys = (char *[]){ "--buffer", "-b" },	.num_keys = 2, .ptr = NULL, .type = 'i', .is_set = false },
    { .keys = (char *[]){ "--play", "-p" },		.num_keys = 2, .ptr = NULL, .type = 'f', .is_set = false },
    { .keys = (char *[]){ "--loop", "-l" },		.num_keys = 2, .ptr = NULL, .type = 'f', .is_set = false },
    { .keys = (char *[]){ "--channel", "-c" },	.num_keys = 2, .ptr = NULL, .type = 'i', .is_set = false },
    { .keys = (char *[]){ "--info", "-i" },		.num_keys = 2, .ptr = NULL, .type = 'f', .is_set = false },
	{ .keys = (char *[]){ "--volume", "-v" },	.num_keys = 2, .ptr = NULL, .type = 'i', .is_set = false },
    { .keys = (char *[]){ "--repeat", "-r" },	.num_keys = 2, .ptr = NULL, .type = 'i', .is_set = false },
    { .keys = (char *[]){ "--upload", "-u" },	.num_keys = 2, .ptr = NULL, .type = 'i', .is_set = false },
    { .keys = (char *[]){ "--stream", "-s" },	.num_keys = 2, .ptr = NULL, .type = 'f', .is_set = false },
    { .keys = (char *[]){ "--quick", "-q" },	.num_keys = 2, .ptr = NULL, .type = 'f', .is_set = false }
};

void parse_args(int argc, char *argv[], cli *cli) {
    
    args[0].ptr = &cli->file;
	args[1].ptr = &cli->buffer;
	args[2].ptr = &cli->play;
    args[3].ptr = &cli->loop;
	args[4].ptr = &cli->channel;
	args[5].ptr = &cli->info;
	args[6].ptr = &cli->volume;
    args[7].ptr = &cli->repeat;
    args[8].ptr = &cli->chunk;
    args[9].ptr = &cli->stream;
    args[10].ptr = &cli->quick;

    for (int i = 1; i < argc; ++i) {
        for (unsigned int j = 0; j < sizeof(args) / sizeof(arg_map); ++j) {
            for (int k = 0; k < args[j].num_keys; ++k) {
                if (strcmp(argv[i], args[j].keys[k]) == 0) {
                    args[j].is_set = true;  // Mark as set
					if (args[j].type == 'f') {
						*(bool *)(args[j].ptr) = true;  // Set flag to true if present
					} else if (i + 1 < argc) {
						i++;
						switch (args[j].type) {
							case 'i':
								*(uint16_t *)(args[j].ptr) = atoi(argv[i]);
								break;
							case 'b':
								*(bool *)(args[j].ptr) = atoi(argv[i]);
								break;
							case 's':
								*(char **)(args[j].ptr) = argv[i];
								break;
						}
                    }
                }
            }
        }
    }
}

static volatile SYSVAR *sv;

int main(int argc, char * argv[])
{
	cli params = {};
	
	sv = vdp_vdu_init();
	if ( vdp_key_init() == -1 ) return 1;

	parse_args(argc, argv, &params);

    // args[0].ptr = &cli->file;
	// args[1].ptr = &cli->buffer;
	// args[2].ptr = &cli->play;
    // args[3].ptr = &cli->loop;
	// args[4].ptr = &cli->channel;
	// args[5].ptr = &cli->info;
	// args[6].ptr = &cli->volume;
    // args[7].ptr = &cli->repeat;
    // args[8].ptr = &cli->upload_chunk;
    // args[9].ptr = &cli->stream;

	//Debug

	// printf("Debug: %s %u %d %d %u %d\r\n", params.file, params.buffer, params.play, params.loop, params.channel, params.info);
	// printf("Is sets: %d %d %d %d %d %d\r\n", args[0].is_set, args[1].is_set, args[2].is_set, args[3].is_set, args[4].is_set, args[5].is_set);

	//End debug

    if (argc == 1) printf("Usage: wavb file.wav\r\nOptions:\r\n\r\n--file or -f:     specify filename (first parameter if not set).\r\n--buffer or -b:   specify buffer id for sample.\r\n--play or -p:     playback after upload.\r\n--loop or -l:     loop audio playback.\r\n--repeat or -r:   repeat sample n times.\r\n--channel or -l:  channel for playback, default 0.\r\n--volume or -v:   volume for playback.\r\n--upload or -u:   upload chunk size (default 4K).\r\n--info or -i:     print debug info.\r\n");

	if (argc > 1 && !args[0].is_set) {
		char ext[] = ".wav";
		char test[5];
		
		strncpy(test, argv[1] + (strlen(argv[1]) - 4), 4);
		test[4] = '\0';
		to_lowercase(test);
		
		if (strncmp(test, ext, 4) == 0) {
			params.file = argv[1];
			args[0].is_set = true;
		}
	}

	if (!args[0].is_set) {

		printf("No file specified.\r\n");
		return 0;

	}

	FILE *file = fopen(params.file, "rb");
    if (file == NULL) {
        printf("Could not open file.\r\n");
        return 0;
    }	

	WavHeader header = parse_wav(file);

	if (args[5].is_set) {
		printf("Format %u, Channels %u, Samplerate %lu, Offset %lu, Size %lu bytes, Calculated duration (ms) %u.\r\n", header.audioFormat, header.numChannels, header.sampleRate, header.extra_header.dataOffset, header.extra_header.dataSize, header.extra_header.duration);
		//fclose(file);
		//return 0;
	}

	int16_t duration = 0;
	uint8_t volume = 100;
    bool quick = false;

    if (!args[8].is_set) params.chunk = DEFAULT_CHUNK_SIZE;
    if (args[10].is_set) quick = true;

    if (args[9].is_set) { //Streaming is requested

        duration = header.extra_header.duration;
		if (args[3].is_set) duration = -1;
        if (args[7].is_set) duration = params.repeat * header.extra_header.duration;
		if (args[6].is_set) volume = params.volume;

		if (args[4].is_set && args[1].is_set) stream_pcm(file, &header, params.buffer, params.chunk, params.channel, volume, duration, quick);
		else if (args[4].is_set && !args[1].is_set) stream_pcm(file, &header, 0, params.chunk, params.channel, volume, duration, quick);
		else if (!args[4].is_set && args[1].is_set) stream_pcm(file, &header, params.buffer, params.chunk, 0, volume, duration, quick);
		else stream_pcm(file, &header, 0, params.chunk, 0, volume, duration, quick);

        fclose(file);
        return 0; 

    }

    //All other modes require uploading in one go

    float before = (float)sv->time;
    if (args[1].is_set) upload_pcm(file, &header, params.buffer, params.chunk, quick);
	else upload_pcm(file, &header, 0, params.chunk, quick);
    if (args[5].is_set) printf("Seconds for upload: %.1f\r\n", ((float)sv->time - before) / 100);
    
    if (args[2].is_set) { //Playback is requested

		if (args[3].is_set) duration = -1;
        if (args[7].is_set) duration = params.repeat * header.extra_header.duration;
		if (args[6].is_set) volume = params.volume;

		if (args[4].is_set && args[1].is_set) play_sample(params.buffer, params.channel, volume, duration);
		else if (args[4].is_set && !args[1].is_set) play_sample(0, params.channel, volume, duration);
		else if (!args[4].is_set && args[1].is_set) play_sample(params.buffer, 0, volume, duration);
		else play_sample(0, 0, volume, duration);
	}

	fclose(file);

	return 0;
}