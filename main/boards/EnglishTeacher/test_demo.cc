#include <Arduino.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <esp_log.h>

#include <opus.h>

#include <Adafruit_GFX.h>

#include "audio/audio_codec.h"
#include "display/display.h"
#include "eteacher/font_manager/font_manager.h"
#include "english-teacher.h"

namespace {

constexpr const char* kTag = "EnglishTeacherDemo";

constexpr const char* kDemoText = "你好，我是小明，my name is xiao ming";

constexpr const char* kImagePath = "resource/image/student.bin";
constexpr int kImageW = 20;
constexpr int kImageH = 20;

constexpr const char* kAudioPath = "resource/audio/sentences/female/sentence_en_1/arm.mp3";

const eteacher::font_manager::Font* SelectBuiltinFont() {
	using eteacher::font_manager::GetBuiltinFont;

	if (auto* f = GetBuiltinFont("wenquanyi_11pt")) {
		return f;
	}
	if (auto* f = GetBuiltinFont("wenquanyi_9pt")) {
		return f;
	}
	return nullptr;
}

// Split by the second occurrence of the Chinese comma "，".
void SplitTwoLines(std::string_view text, std::string_view* out_line1, std::string_view* out_line2) {
	const std::string_view delim = "，";
	size_t pos = text.find(delim);
	if (pos == std::string_view::npos) {
		*out_line1 = text;
		*out_line2 = {};
		return;
	}
	size_t pos2 = text.find(delim, pos + delim.size());
	if (pos2 == std::string_view::npos) {
		*out_line1 = text.substr(0, pos);
		*out_line2 = text.substr(pos + delim.size());
		return;
	}
	*out_line1 = text.substr(0, pos2);
	*out_line2 = text.substr(pos2 + delim.size());
}

bool DrawBin20x20(Adafruit_GFX& gfx, int16_t x0, int16_t y0, const std::vector<uint8_t>& data) {
	// Accept two common raw formats:
	// - 1bpp packed: 20*20 bits = 50 bytes (MSB is x=0 per row)
	// - 8bpp grayscale: 20*20 = 400 bytes (0..255)
	if (data.size() == 50) {
		const int stride = (kImageW + 7) / 8;
		for (int y = 0; y < kImageH; ++y) {
			for (int x = 0; x < kImageW; ++x) {
				const uint8_t b = data[y * stride + (x / 8)];
				const uint8_t mask = static_cast<uint8_t>(0x80 >> (x % 8));
				const bool on = (b & mask) != 0;
				if (on) {
					gfx.drawPixel(x0 + x, y0 + y, GxEPD_BLACK);
				}
			}
		}
		return true;
	}

	if (data.size() == 400) {
		for (int y = 0; y < kImageH; ++y) {
			for (int x = 0; x < kImageW; ++x) {
				const uint8_t v = data[y * kImageW + x];
				// Simple threshold.
				if (v < 128) {
					gfx.drawPixel(x0 + x, y0 + y, GxEPD_BLACK);
				}
			}
		}
		return true;
	}

	ESP_LOGW(kTag, "Unexpected image size=%u for %s", (unsigned)data.size(), kImagePath);
	return false;
}

bool IsOgg(const std::vector<uint8_t>& data) {
	return data.size() >= 4 && data[0] == 'O' && data[1] == 'g' && data[2] == 'g' && data[3] == 'S';
}

bool LooksLikeMp3(const std::vector<uint8_t>& data) {
	if (data.size() < 3) return false;
	if (data[0] == 'I' && data[1] == 'D' && data[2] == '3') return true;
	// Frame sync 0xFFE (allow 0xFF FB / 0xFF F3 / 0xFF F2)
	if (data.size() >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) return true;
	return false;
}

// Minimal Ogg/Opus playback: parse pages, extract Opus packets, decode via libopus, push PCM to codec.
// This is only used when the file is actually an Ogg/Opus stream (even if it ends with .mp3).
bool PlayOggOpusFromMemory(const std::vector<uint8_t>& ogg, AudioCodec* codec) {
	if (!codec) {
		ESP_LOGE(kTag, "No AudioCodec");
		return false;
	}

	auto pick_rate = [](int desired) {
		switch (desired) {
			case 8000:
			case 12000:
			case 16000:
			case 24000:
			case 48000:
				return desired;
			default:
				return 16000;
		}
	};

	const int out_rate = pick_rate(codec->output_sample_rate());
	int channels = 1;
	bool seen_head = false;
	bool seen_tags = false;

	int opus_err = 0;
	OpusDecoder* dec = opus_decoder_create(out_rate, 1, &opus_err);
	if (!dec || opus_err != OPUS_OK) {
		ESP_LOGE(kTag, "opus_decoder_create failed: %d", opus_err);
		if (dec) opus_decoder_destroy(dec);
		return false;
	}

	codec->EnableOutput(true);
	codec->Start();

	const uint8_t* buf = ogg.data();
	const size_t size = ogg.size();
	size_t offset = 0;

	auto find_page = [&](size_t start) -> size_t {
		for (size_t i = start; i + 4 <= size; ++i) {
			if (buf[i] == 'O' && buf[i + 1] == 'g' && buf[i + 2] == 'g' && buf[i + 3] == 'S') {
				return i;
			}
		}
		return static_cast<size_t>(-1);
	};

	const int max_frame_samples = (out_rate * 120) / 1000; // up to 120ms
	std::vector<int16_t> pcm;
	pcm.resize(static_cast<size_t>(max_frame_samples));

	while (true) {
		const size_t pos = find_page(offset);
		if (pos == static_cast<size_t>(-1)) break;
		offset = pos;
		if (offset + 27 > size) break;

		const uint8_t* page = buf + offset;
		const uint8_t page_segments = page[26];
		const size_t seg_table_off = offset + 27;
		if (seg_table_off + page_segments > size) break;

		size_t body_size = 0;
		for (size_t i = 0; i < page_segments; ++i) {
			body_size += page[27 + i];
		}

		const size_t body_off = seg_table_off + page_segments;
		if (body_off + body_size > size) break;

		// Parse packets via lacing values
		size_t cur = body_off;
		size_t seg_idx = 0;
		while (seg_idx < page_segments) {
			size_t pkt_len = 0;
			const size_t pkt_start = cur;
			bool continued = false;
			do {
				const uint8_t l = page[27 + seg_idx++];
				pkt_len += l;
				cur += l;
				continued = (l == 255);
			} while (continued && seg_idx < page_segments);

			if (pkt_len == 0) continue;
			if (pkt_start + pkt_len > size) {
				seg_idx = page_segments;
				break;
			}

			const uint8_t* pkt = buf + pkt_start;

			if (!seen_head) {
				if (pkt_len >= 19 && std::memcmp(pkt, "OpusHead", 8) == 0) {
					seen_head = true;
					channels = pkt[9];
					ESP_LOGI(kTag, "OpusHead channels=%d", channels);
					if (channels != 1) {
						ESP_LOGW(kTag, "Only mono output is supported in demo; will use left channel");
					}
				}
				continue;
			}
			if (!seen_tags) {
				if (pkt_len >= 8 && std::memcmp(pkt, "OpusTags", 8) == 0) {
					seen_tags = true;
				}
				continue;
			}

			// Opus audio packet
			int frame = opus_decode(dec, pkt, static_cast<opus_int32>(pkt_len), pcm.data(), max_frame_samples, 0);
			if (frame < 0) {
				ESP_LOGW(kTag, "opus_decode failed: %d", frame);
				continue;
			}
			// frame is per-channel samples; we created decoder as mono.
			std::vector<int16_t> out;
			out.assign(pcm.begin(), pcm.begin() + frame);
			codec->OutputData(out);
		}

		offset = body_off + body_size;
	}

	opus_decoder_destroy(dec);
	return seen_head;
}

} // namespace

void EnglishTeacher_RunTestDemo() {
	EnglishTeacherBoard& board_instance = EnglishTeacherBoard::GetInstance();
	ESP_LOGI(kTag, "Demo start");

	auto* display = board_instance.GetDisplay();
	auto* codec = board_instance.GetAudioCodec();
	auto* sd = board_instance.GetSd();
	if (!display) {
		ESP_LOGE(kTag, "No display from board");
		return;
	}
	if (!sd) {
		ESP_LOGE(kTag, "No SD from board");
		display->ShowNotification("SD not available");
		return;
	}

	const auto* font = SelectBuiltinFont();
	if (!font || !font->Ready()) {
		ESP_LOGE(kTag, "Builtin font not available or not ready");
		display->ShowNotification("font not ready");
		return;
	}

	std::vector<uint8_t> image;
	const bool image_ok = sd->ReadImage(kImagePath, &image, 4096);
	ESP_LOGI(kTag, "SD image %s (%s, %u bytes)", image_ok ? "OK" : "FAILED", kImagePath, (unsigned)image.size());

	// Render: text + optional image.
	{
		DisplayLockGuard guard(display);
		auto& epd = display->Driver();

		std::string_view l1, l2;
		SplitTwoLines(kDemoText, &l1, &l2);

		epd.setFullWindow();
		epd.firstPage();
		do {
			epd.fillScreen(GxEPD_WHITE);
			font->DrawUtf8(epd, 10, 40, l1, GxEPD_BLACK);
			if (!l2.empty()) {
				font->DrawUtf8(epd, 10, 75, l2, GxEPD_BLACK);
			}

			if (image_ok) {
				// Draw image below text.
				const int16_t x = 10;
				const int16_t y = 110;
				epd.fillRect(x, y, kImageW, kImageH, GxEPD_WHITE);
				const bool drawn = DrawBin20x20(epd, x, y, image);
				if (drawn) {
					ESP_LOGW(kTag, "TEST2 OK: SD image loaded+drawn (%s)", kImagePath);
				}
			} else {
				font->DrawUtf8(epd, 10, 120, "SD image read failed", GxEPD_BLACK);
			}
		} while (epd.nextPage());
	}

	ESP_LOGW(kTag, "TEST1 OK: font_manager text rendered");

	// Audio: read file from SD and try to play if it is Ogg/Opus (even if it ends with .mp3).
	std::vector<uint8_t> audio;
	const bool audio_ok = sd->ReadMp3(kAudioPath, &audio, 2 * 1024 * 1024);
	ESP_LOGI(kTag, "SD audio %s (%s, %u bytes)", audio_ok ? "OK" : "FAILED", kAudioPath, (unsigned)audio.size());
	if (!audio_ok) {
		display->ShowNotification("SD audio read failed");
		return;
	}

	if (IsOgg(audio)) {
		ESP_LOGI(kTag, "Audio looks like Ogg/Opus, attempting playback");
		const bool played = PlayOggOpusFromMemory(audio, codec);
		ESP_LOGI(kTag, "Ogg/Opus playback %s", played ? "DONE" : "FAILED");
		if (played) {
			ESP_LOGW(kTag, "TEST3 OK: SD audio played (Ogg/Opus) (%s)", kAudioPath);
		}
		return;
	}

	if (LooksLikeMp3(audio)) {
		ESP_LOGW(kTag, "File looks like MP3, but this firmware doesn't include an MP3 decoder yet");
		display->ShowNotification("MP3 not supported; use OGG/Opus");
		return;
	}

	ESP_LOGW(kTag, "Unknown audio format: %02X %02X %02X %02X", audio[0], audio[1], audio[2], audio[3]);
	display->ShowNotification("unknown audio format");
}

