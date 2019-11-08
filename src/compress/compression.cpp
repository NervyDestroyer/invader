// SPDX-License-Identifier: GPL-3.0-only

#include <invader/compress/compression.hpp>
#include <invader/map/map.hpp>
#include <zstd.h>
#include <cstdio>
#include <filesystem>

namespace Invader::Compression {
    static void compress_header(const std::byte *header_input, std::byte *header_output, std::size_t decompressed_size) {
        // Check the header
        const auto &header = *reinterpret_cast<const HEK::CacheFileHeader *>(header_input);
        if(!header.valid()) {
            throw InvalidMapException();
        }

        auto new_engine_version = header.engine.read();
        switch(header.engine.read()) {
            case HEK::CacheFileEngine::CACHE_FILE_CUSTOM_EDITION:
                new_engine_version = HEK::CacheFileEngine::CACHE_FILE_CUSTOM_EDITION_COMPRESSED;
                break;
            case HEK::CacheFileEngine::CACHE_FILE_RETAIL:
                new_engine_version = HEK::CacheFileEngine::CACHE_FILE_RETAIL_COMPRESSED;
                break;
            case HEK::CacheFileEngine::CACHE_FILE_DEMO:
                new_engine_version = HEK::CacheFileEngine::CACHE_FILE_DEMO_COMPRESSED;
                break;
            case HEK::CacheFileEngine::CACHE_FILE_DARK_CIRCLET:
                if(header.decompressed_file_size.read() > 0) {
                    throw MapNeedsDecompressedException();
                }
                break;
            case HEK::CacheFileEngine::CACHE_FILE_CUSTOM_EDITION_COMPRESSED:
            case HEK::CacheFileEngine::CACHE_FILE_RETAIL_COMPRESSED:
            case HEK::CacheFileEngine::CACHE_FILE_DEMO_COMPRESSED:
                throw MapNeedsDecompressedException();
            default:
                throw UnsupportedMapEngineException();
        }

        // Write the header
        auto &header_out = *reinterpret_cast<HEK::CacheFileHeader *>(header_output);
        header_out = header;
        header_out.engine = new_engine_version;
        header_out.foot_literal = HEK::CacheFileLiteral::CACHE_FILE_FOOT;
        header_out.head_literal = HEK::CacheFileLiteral::CACHE_FILE_HEAD;
        if(decompressed_size > UINT32_MAX) {
            throw MaximumFileSizeException();
        }
        header_out.decompressed_file_size = static_cast<std::uint32_t>(decompressed_size);
    }

    static void decompress_header(const std::byte *header_input, std::byte *header_output) {
        // Check to see if we can't even fit the header
        auto header_copy = *reinterpret_cast<const HEK::CacheFileHeader *>(header_input);

        // Figure out the new engine version
        auto new_engine_version = header_copy.engine.read();
        switch(header_copy.engine.read()) {
            case HEK::CacheFileEngine::CACHE_FILE_RETAIL:
            case HEK::CacheFileEngine::CACHE_FILE_CUSTOM_EDITION:
            case HEK::CacheFileEngine::CACHE_FILE_DEMO:
                throw MapNeedsCompressedException();
                break;
            case HEK::CacheFileEngine::CACHE_FILE_DARK_CIRCLET:
                if(header_copy.decompressed_file_size.read() == 0) {
                    throw MapNeedsCompressedException();
                }
                break;
            case HEK::CacheFileEngine::CACHE_FILE_CUSTOM_EDITION_COMPRESSED:
                new_engine_version = HEK::CacheFileEngine::CACHE_FILE_CUSTOM_EDITION;
                break;
            case HEK::CacheFileEngine::CACHE_FILE_RETAIL_COMPRESSED:
                new_engine_version = HEK::CacheFileEngine::CACHE_FILE_RETAIL;
                break;
            case HEK::CacheFileEngine::CACHE_FILE_DEMO_COMPRESSED:
                new_engine_version = HEK::CacheFileEngine::CACHE_FILE_DEMO;
                break;
            default:
                // Check if it's an uncompressed demo map?
                if(static_cast<HEK::CacheFileHeader>(*reinterpret_cast<const HEK::CacheFileDemoHeader *>(header_input)).engine.read() == HEK::CacheFileEngine::CACHE_FILE_DEMO) {
                    throw MapNeedsCompressedException();
                }

                // Give up
                throw UnsupportedMapEngineException();
        }

        // Determine if the file size isn't set correctly
        if(header_copy.decompressed_file_size < sizeof(header_copy) || !header_copy.valid()) {
            throw InvalidMapException();
        }

        // Set the file size to 0 and the engine to the new thing
        header_copy.decompressed_file_size = 0;
        header_copy.engine = new_engine_version;

        // if demo, convert the header, otherwise copy the header
        if(new_engine_version == HEK::CACHE_FILE_DEMO) {
            header_copy.foot_literal = HEK::CacheFileLiteral::CACHE_FILE_FOOT_DEMO;
            header_copy.head_literal = HEK::CacheFileLiteral::CACHE_FILE_HEAD_DEMO;
            *reinterpret_cast<HEK::CacheFileDemoHeader *>(header_output) = header_copy;
        }
        else {
            *reinterpret_cast<HEK::CacheFileHeader *>(header_output) = header_copy;
        }
    }

    constexpr std::size_t HEADER_SIZE = sizeof(HEK::CacheFileHeader);

    std::size_t compress_map_data(const std::byte *data, std::size_t data_size, std::byte *output, std::size_t output_size, int compression_level) {
        // Load the data
        auto map = Map::map_with_pointer(const_cast<std::byte *>(data), data_size);

        // Allocate the data
        compress_header(reinterpret_cast<const std::byte *>(&map.get_cache_file_header()), output, data_size);

        // Immediately compress it
        auto compressed_size = ZSTD_compress(output + HEADER_SIZE, output_size - HEADER_SIZE, data + HEADER_SIZE, data_size - HEADER_SIZE, compression_level);
        if(ZSTD_isError(compressed_size)) {
            throw CompressionFailureException();
        }

        // Done
        return compressed_size + HEADER_SIZE;
    }

    std::size_t decompress_map_data(const std::byte *data, std::size_t data_size, std::byte *output, std::size_t output_size) {
        // Check the header
        const auto *header = reinterpret_cast<const HEK::CacheFileHeader *>(data);
        if(sizeof(*header) > data_size || !header->valid()) {
            auto demo_header = static_cast<const HEK::CacheFileHeader>(*reinterpret_cast<const HEK::CacheFileDemoHeader *>(header));
            if(demo_header.valid() && demo_header.engine == HEK::CacheFileEngine::CACHE_FILE_DEMO) {
                throw MapNeedsCompressedException();
            }
            throw InvalidMapException();
        }

        decompress_header(data, output);

        // Immediately decompress
        auto decompressed_size = ZSTD_decompress(output + HEADER_SIZE, output_size - HEADER_SIZE, data + HEADER_SIZE, data_size - HEADER_SIZE);
        if(ZSTD_isError(decompressed_size) || (decompressed_size + HEADER_SIZE) != header->decompressed_file_size) {
            throw DecompressionFailureException();
        }

        // Done
        return decompressed_size + HEADER_SIZE;
    }

    std::vector<std::byte> compress_map_data(const std::byte *data, std::size_t data_size, int compression_level) {
        // Allocate the data
        std::vector<std::byte> new_data(ZSTD_compressBound(data_size - HEADER_SIZE) + HEADER_SIZE);

        // Compress
        auto compressed_size = compress_map_data(data, data_size, new_data.data(), new_data.size(), compression_level);

        // Resize and return it
        new_data.resize(compressed_size);

        return new_data;
    }

    std::vector<std::byte> decompress_map_data(const std::byte *data, std::size_t data_size) {
        // Allocate and decompress using data from the header
        const auto *header = reinterpret_cast<const HEK::CacheFileHeader *>(data);
        std::vector<std::byte> new_data(header->decompressed_file_size);
        decompress_header(data, new_data.data());

        // Compress
        auto decompressed_size = decompress_map_data(data, data_size, new_data.data(), new_data.size());

        // Shrink the buffer to the new size
        new_data.resize(decompressed_size);

        return new_data;
    }

    void decompress_map_file(const char *input, const char *output) {
        // Open the input file
        std::FILE *input_file = std::fopen(input, "rb");
        if(!input_file) {
            throw FailedToOpenFileException();
        }

        // Get the size
        std::size_t total_size = std::filesystem::file_size(input);

        // Read the input file header
        HEK::CacheFileHeader header_input;
        if(std::fread(&header_input, sizeof(header_input), 1, input_file) != 1) {
            std::fclose(input_file);
            throw DecompressionFailureException();
        }

        // Make the output header and write it
        std::byte header_output[HEADER_SIZE];
        try {
            decompress_header(reinterpret_cast<std::byte *>(&header_input), header_output);
        }
        catch (std::exception &) {
            std::fclose(input_file);
            throw;
        }

        // Now, open the output file
        std::FILE *output_file = std::fopen(output, "wb");
        if(!output_file) {
            std::fclose(input_file);
            throw FailedToOpenFileException();
        }

        // Write the header
        if(std::fwrite(header_output, sizeof(header_output), 1, output_file) != 1) {
            std::fclose(input_file);
            std::fclose(output_file);
            throw DecompressionFailureException();
        }

        // Allocate and init a stream
        auto decompression_stream = ZSTD_createDStream();
        const std::size_t init = ZSTD_initDStream(decompression_stream);

        std::size_t total_read = HEADER_SIZE;
        auto read_data = [&input_file, &output_file, &decompression_stream, &total_read](std::byte *where, std::size_t size) {
            if(std::fread(where, size, 1, input_file) != 1) {
                std::fclose(input_file);
                std::fclose(output_file);
                ZSTD_freeDStream(decompression_stream);
                throw DecompressionFailureException();
            }
            total_read += size;
        };

        auto write_data = [&input_file, &output_file, &decompression_stream](const std::byte *where, std::size_t size) {
            if(std::fwrite(where, size, 1, output_file) != 1) {
                std::fclose(input_file);
                std::fclose(output_file);
                ZSTD_freeDStream(decompression_stream);
                throw DecompressionFailureException();
            }
        };

        while(total_read < total_size) {
            // Make some input/output data thingy
            std::vector<std::byte> input_data(init);
            std::vector<std::byte> output_data(ZSTD_DStreamOutSize());

            // Read the first bit
            read_data(input_data.data(), input_data.size());

            for(;;) {
                ZSTD_inBuffer_s input_buffer = {};
                ZSTD_outBuffer_s output_buffer = {};
                input_buffer.src = input_data.data();
                input_buffer.size = input_data.size();
                output_buffer.dst = output_data.data();
                output_buffer.size = output_data.size();

                // Get the output
                std::size_t q = ZSTD_decompressStream(decompression_stream, &output_buffer, &input_buffer);
                if(ZSTD_isError(q)) {
                    std::fclose(input_file);
                    std::fclose(output_file);
                    ZSTD_freeDStream(decompression_stream);
                    throw DecompressionFailureException();
                }

                // Write it
                if(output_buffer.pos) {
                    write_data(reinterpret_cast<std::byte *>(output_buffer.dst), output_buffer.pos);
                }

                // If it's > 0, we need more data
                if(q > 0) {
                    input_data.clear();
                    input_data.insert(input_data.end(), q, std::byte());
                    read_data(input_data.data(), q);
                }
                else {
                    break;
                }
            }
        }

        // Close the stream and the files
        std::fclose(input_file);
        std::fclose(output_file);
        ZSTD_freeDStream(decompression_stream);
    }
}
