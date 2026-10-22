#include "pages/PageRenderer.h"

#include <stdio.h>

#include <string>

#include "Fonts/TomThumb.h"
#include "live/LiveFormat.h"
#include "timer/TimeMath.h"

namespace PageRenderer {
namespace {

// 取字形实际可见列数（TomThumb 的 glyph.width 恒为 8，需按位图置位扫描）。
// 与 v2 TimerPage::glyphVisibleWidth 完全一致，保证像素级兼容。
int glyphVisibleWidthImpl(char c) {
	const unsigned char uc = static_cast<unsigned char>(c);
	if (uc < TomThumb.first || uc > TomThumb.last) {
		return 0;
	}
	const GFXglyph* glyph = &TomThumb.glyph[uc - TomThumb.first];
	const uint8_t* bitmap = &TomThumb.bitmap[glyph->bitmapOffset];
	int width = 0;
	for (int row = 0; row < glyph->height; ++row) {
		const uint8_t bits = bitmap[row];
		for (int col = 7; col >= 0; --col) {
			if (bits & static_cast<uint8_t>(0x80 >> col)) {
				if (col + 1 > width) {
					width = col + 1;
				}
				break;
			}
		}
	}
	return width;
}

std::string truncateToWidthSpaced(const std::string& text, int scale,
		int spacing, int maxWidth) {
	std::string out = text;
	while (!out.empty() && measureText(out, scale, spacing) > maxWidth) {
		out.erase(out.size() - 1);
	}
	return out;
}

void renderClockPage(const TimerConfig& config, long long nowUtc,
		bool timeValid, Surface& surface) {
	std::string text;
	uint32_t color = kColorTimeInvalid;
	if (timeValid) {
		text = formatEpochLocalTime(nowUtc, config.tzOffsetMinutes);
		color = config.colorClock;
	}
	if (text.empty()) {
		text = "--:--:--";
		color = kColorTimeInvalid;
	}
	drawScaledText(surface, text, 2, 2, color);
}

void renderDatetimePage(const TimerConfig& config, long long nowUtc,
		bool timeValid, Surface& surface) {
	if (!timeValid) {
		drawTextCentered(surface, "--:--:--", 10, 1, 1, kColorTimeInvalid);
		return;
	}
	const std::string timeText =
			formatEpochLocalTime(nowUtc, config.tzOffsetMinutes);
	LocalDateTime local;
	if (timeText.empty() ||
			!epochToLocalDateTime(nowUtc, config.tzOffsetMinutes, local)) {
		drawTextCentered(surface, "--:--:--", 10, 1, 1, kColorTimeInvalid);
		return;
	}
	// 时间大字放上半屏（scale2 占 0..9 行），日期小字放最底部（占 11..15 行）。
	int spacing = 2;
	while (spacing > 0 && measureText(timeText, 2, spacing) > kScreenWidth) {
		--spacing;
	}
	drawTextCentered(surface, timeText, 10, 2, spacing, config.colorClock);
	drawTextCentered(surface, formatDate(local), 15, 1, 0, config.colorDim);
}

void renderSplitPage(const TimerConfig& config, long long nowUtc,
		const Stage& stage, bool timeValid, Surface& surface) {
	// 分屏上下两行都尽量占满左右：时间字符串用更大的字距（先 3，放不下再降），
	// 仍放不下才截断；HH:MM:SS 在 spacing=3 时约 45px，接近 52px 全宽。
	std::string clockSpread;
	if (timeValid) {
		clockSpread = formatEpochLocalTime(nowUtc, config.tzOffsetMinutes);
	}
	auto drawSpreadTime = [&surface](const std::string& text, int baseline,
			uint32_t rgb) {
		const int spacing = timeSpacingForWidth(text, 3);
		if (measureText(text, 1, spacing) > kScreenWidth) {
			drawTextFitted(surface, text, 0, kScreenWidth, baseline, rgb);
			return;
		}
		drawTextCentered(surface, text, baseline, 1, spacing, rgb);
	};
	if (!timeValid || clockSpread.empty()) {
		// 时间无效：上半暗灰占位，下半留空（不猜日期）
		drawSpreadTime("--:--:--", kTopBaseline, kColorTimeInvalid);
		return;
	}
	drawSpreadTime(clockSpread, kTopBaseline, config.colorClock);

	if (stage.kind == STAGE_COUNTDOWN) {
		bool useSmallFont = false;
		const std::string text =
				formatCountdown(stage.remainSeconds, useSmallFont);
		const uint32_t color =
				stage.warning ? config.colorWarning : config.colorCountdown;
		drawSpreadTime(text, kBottomBaseline, color);
		return;
	}
	if (stage.kind == STAGE_COUNTUP) {
		bool useSmallFont = false;
		const std::string text =
				formatCountdown(stage.elapsedSeconds, useSmallFont);
		drawSpreadTime(text, kBottomBaseline, config.colorCountup);
		return;
	}
	// 没有任何目标：显示 NO COUNTDOWN（暗色），不再显示日期。
	drawTextCentered(surface, "NO COUNTDOWN", kBottomBaseline, 1, 0,
			config.colorDim);
}

void renderCountdownPage(const TimerConfig& config, long long nowUtc,
		const Stage& stage, bool timeValid, Surface& surface) {
	if (!timeValid) {
		drawScaledText(surface, "--:--:--", 2, 2, kColorTimeInvalid);
		return;
	}
	if (stage.kind == STAGE_COUNTDOWN) {
		bool useSmallFont = false;
		const std::string text =
				formatCountdown(stage.remainSeconds, useSmallFont);
		const int scale = (useSmallFont || stage.smallFont) ? 1 : 2;
		const uint32_t color =
				stage.warning ? config.colorWarning : config.colorCountdown;
		drawScaledText(surface, text, scale, scale, color);
		return;
	}
	if (stage.kind == STAGE_COUNTUP) {
		bool useSmallFont = false;
		const std::string text =
				formatCountdown(stage.elapsedSeconds, useSmallFont);
		const int scale = (useSmallFont || stage.smallFont) ? 1 : 2;
		drawScaledText(surface, text, scale, scale, config.colorCountup);
		return;
	}
	// 没有任何目标：显示 NO COUNTDOWN（暗色），不再自动回时钟。
	drawTextCentered(surface, "NO COUNTDOWN", 10, 1, 0, config.colorDim);
}

void renderLivePage(const TimerConfig& config, const PageConfig& page,
		const LiveSnapshot& live, Surface& surface) {
	int configured = 0;
	for (size_t i = 0; i < page.rooms.size() &&
			i < static_cast<size_t>(kLiveRowCount); ++i) {
		if (LiveFormat::isCellConfigured(page.rooms[i])) {
			++configured;
		}
	}
	if (configured == 0) {
		// 整页没有可轮询的房间：NO LIVE 整屏居中（dim）
		drawTextCentered(surface, "NO LIVE", 10, 1, 1, config.colorDim);
		return;
	}

	for (size_t i = 0; i < page.rooms.size() &&
			i < static_cast<size_t>(kLiveRowCount); ++i) {
		const LiveRoomConfig& room = page.rooms[i];
		if (!LiveFormat::isCellConfigured(room)) {
			continue;  // 未配置 / disabled：整行留黑
		}
		const LiveMetric metric = LiveFormat::effectiveMetric(page, room);
		const LiveMetricValue value =
				LiveFormat::selectMetricValue(live.rooms[i], metric);
		const std::string label = LiveFormat::labelFor(room);
		const std::string valueText = LiveFormat::formatMetric(value, metric);

		LiveCellLayout layout;
		layoutLiveCell(label, valueText, static_cast<int>(i), layout);

		const int baseline =
				(i == 0) ? kLiveTopBaseline : kLiveBottomBaseline;
		const bool hasData = value.valid && isMetricDisplayable(metric);
		uint32_t valueColor = config.colorDim;
		if (hasData) {
			valueColor = (live.rooms[i].state == LIVE_ROOM_STALE)
					? config.colorDim
					: kLiveValueColor;
		}
		drawText(surface, layout.label, layout.labelX, baseline, 1, 1,
				LiveFormat::platformColor(room.platform));
		drawText(surface, valueText, layout.valueX, baseline, 1, 1,
				valueColor);
	}
}

}  // namespace

int timeSpacingForWidth(const std::string& text, int maxSpacing) {
	int spacing = maxSpacing < 0 ? 0 : maxSpacing;
	while (spacing > 0 && measureText(text, 1, spacing) > kScreenWidth) {
		--spacing;
	}
	return spacing;
}

int glyphVisibleWidth(char c) {
	return glyphVisibleWidthImpl(c);
}

int charAdvance(char c, int scale) {
	if (scale < 1) {
		scale = 1;
	}
	if (c == ' ') {
		return 2 * scale;
	}
	const int width = glyphVisibleWidthImpl(c);
	return (width > 0 ? width : 2) * scale;
}

int measureText(const std::string& text, int scale, int spacing) {
	if (scale < 1) {
		scale = 1;
	}
	int total = 0;
	const int count = static_cast<int>(text.size());
	for (int i = 0; i < count; ++i) {
		total += charAdvance(text[i], scale);
		if (i < count - 1) {
			total += spacing;
		}
	}
	return total;
}

void drawText(Surface& surface, const std::string& text, int x, int baseline,
		int scale, int spacing, uint32_t rgb) {
	if (scale < 1 || text.empty()) {
		return;
	}
	const Color color(rgb);
	int cursorX = x;
	const int count = static_cast<int>(text.size());
	for (int i = 0; i < count; ++i) {
		const char c = text[i];
		const unsigned char uc = static_cast<unsigned char>(c);
		if (uc >= TomThumb.first && uc <= TomThumb.last) {
			const GFXglyph* glyph = &TomThumb.glyph[uc - TomThumb.first];
			const uint8_t* bitmap = &TomThumb.bitmap[glyph->bitmapOffset];
			const int visW = glyphVisibleWidthImpl(c);
			for (int gy = 0; gy < glyph->height; ++gy) {
				const uint8_t rowBits = bitmap[gy];
				for (int gx = 0; gx < visW; ++gx) {
					if (!(rowBits & static_cast<uint8_t>(0x80 >> gx))) {
						continue;
					}
					const int originX =
							cursorX + glyph->xOffset * scale + gx * scale;
					const int originY =
							baseline + glyph->yOffset * scale + gy * scale;
					for (int dy = 0; dy < scale; ++dy) {
						for (int dx = 0; dx < scale; ++dx) {
							surface.setPixel(originX + dx, originY + dy,
									color);
						}
					}
				}
			}
		}
		cursorX += charAdvance(c, scale);
		if (i < count - 1) {
			cursorX += spacing;
		}
	}
}

void drawTextCentered(Surface& surface, const std::string& text, int baseline,
		int scale, int spacing, uint32_t rgb) {
	const int width = measureText(text, scale, spacing);
	drawText(surface, text, (kScreenWidth - width) / 2, baseline, scale,
			spacing, rgb);
}

void drawScaledText(Surface& surface, const std::string& text, int scale,
		int spacing, uint32_t rgb) {
	if (scale < 1 || text.empty()) {
		return;
	}
	// 超宽时自动收紧字距，仍超宽则按屏幕宽度截断（与 v2 一致）
	int gap = spacing;
	int width = measureText(text, scale, gap);
	while (width > kScreenWidth && gap > 0) {
		--gap;
		width = measureText(text, scale, gap);
	}
	if (width > kScreenWidth) {
		width = kScreenWidth;
	}
	const int top = (kScreenHeight - 5 * scale) / 2;
	const int baseline = top + 5 * scale;
	const int cursorX = (kScreenWidth - width) / 2;
	drawText(surface, text, cursorX, baseline, scale, gap, rgb);
}

std::string truncateToWidth(const std::string& text, int maxWidth) {
	return truncateToWidthSpaced(text, 1, 1, maxWidth);
}

void drawTextFitted(Surface& surface, const std::string& text, int x, int width,
		int baseline, uint32_t rgb) {
	drawText(surface, truncateToWidth(text, width), x, baseline, 1, 1, rgb);
}

LiveCellLayout::LiveCellLayout()
	: gap(2), labelWidth(0), valueWidth(0), labelX(0), valueX(0),
	  fits(false) {
}

bool layoutLiveCell(const std::string& label, const std::string& value,
		int rowIndex, LiveCellLayout& out) {
	if (rowIndex < 0 || rowIndex >= kLiveRowCount) {
		return false;
	}
	std::string full = label;
	if (full.size() > static_cast<size_t>(kMaxRoomLabelChars)) {
		full = full.substr(0, static_cast<size_t>(kMaxRoomLabelChars));
	}
	if (full.empty()) {
		full = "?";
	}
	out.valueWidth = measureText(value, 1, 1);
	out.labelX = kLiveTextLeft;
	out.valueX = kLiveTextRight - out.valueWidth + 1;

	for (int len = static_cast<int>(full.size()); len >= 1; --len) {
		const std::string candidate =
				full.substr(0, static_cast<size_t>(len));
		const int labelWidth = measureText(candidate, 1, 1);
		for (int gap = 2; gap >= 1; --gap) {
			if (labelWidth + gap + out.valueWidth <= kLiveTextWidth) {
				out.label = candidate;
				out.gap = gap;
				out.labelWidth = labelWidth;
				out.fits = true;
				return true;
			}
		}
	}

	// 理论上到不了这里（label 1 字符 + gap 2 + value <= 21px）；兜底不截断数值。
	out.label = full.substr(0, 1);
	out.labelWidth = measureText(out.label, 1, 1);
	out.gap = 2;
	out.fits = (out.labelWidth + out.gap + out.valueWidth <= kLiveTextWidth);
	return true;
}

bool renderPage(const TimerConfig& config, int pageIndex, long long nowUtc,
		const Stage& stage, bool timeValid, const LiveSnapshot& live,
		Surface& surface) {
	surface.clear();
	const PageConfig* page = pageAt(config, pageIndex);
	if (page == NULL) {
		return false;
	}
	switch (page->type) {
		case PAGE_TYPE_CLOCK:
			renderClockPage(config, nowUtc, timeValid, surface);
			return true;
		case PAGE_TYPE_CLOCK_COUNTDOWN:
			renderSplitPage(config, nowUtc, stage, timeValid, surface);
			return true;
		case PAGE_TYPE_COUNTDOWN:
			renderCountdownPage(config, nowUtc, stage, timeValid, surface);
			return true;
		case PAGE_TYPE_DATETIME:
			renderDatetimePage(config, nowUtc, timeValid, surface);
			return true;
		case PAGE_TYPE_LIVE:
			renderLivePage(config, *page, live, surface);
			return true;
	}
	return false;
}

void renderPageBanner(int index, int count, const std::string& shortTitle,
		const TimerConfig& config, Surface& surface) {
	surface.clear();
	std::string title = shortTitle.empty() ? std::string("PAGE") : shortTitle;
	title = truncateToWidth(title, kScreenWidth);
	drawTextCentered(surface, title, kTopBaseline, 1, 1, config.colorClock);

	int total = count;
	if (total < 1) {
		total = 1;
	}
	int current = index + 1;
	if (current < 1) {
		current = 1;
	}
	if (current > total) {
		current = total;
	}
	char buf[24];
	snprintf(buf, sizeof(buf), "%d/%d", current, total);
	drawTextCentered(surface, buf, kBottomBaseline, 1, 1, config.colorClock);
}

void renderOverlayText(const std::string& text, uint32_t rgb,
		Surface& surface) {
	surface.clear();

	// 最多两行：优先按换行拆；没有换行再按第一个空格拆（"IP 192.168.1.100" /
	// "NO WIFI"）。每行自动截断到 52 像素内（与 v2 renderOverlay 一致）。
	std::string lines[2];
	int lineCount = 0;
	size_t split = text.find('\n');
	if (split == std::string::npos) {
		split = text.find(' ');
	}
	if (split != std::string::npos && split > 0 && split + 1 < text.size()) {
		lines[0] = text.substr(0, split);
		lines[1] = text.substr(split + 1);
		lineCount = 2;
	} else {
		lines[0] = text;
		lineCount = 1;
	}
	for (int i = 0; i < lineCount; ++i) {
		lines[i] = truncateToWidthSpaced(lines[i], 1, 0, kScreenWidth);
	}
	if (lines[0].empty()) {
		return;
	}

	if (lineCount == 1) {
		const int width = measureText(lines[0], 1, 0);
		drawText(surface, lines[0], (kScreenWidth - width) / 2, 10, 1, 0, rgb);
	} else {
		const int w0 = measureText(lines[0], 1, 0);
		const int w1 = measureText(lines[1], 1, 0);
		drawText(surface, lines[0], (kScreenWidth - w0) / 2, 6, 1, 0, rgb);
		drawText(surface, lines[1], (kScreenWidth - w1) / 2, 14, 1, 0, rgb);
	}
}

void renderOverlayTextScrolled(const std::string& text, uint32_t rgb,
		int offsetPx, Surface& surface) {
	if (text.empty()) {
		surface.clear();
		return;
	}
	// 两行内容（AP SSID/密码）仍按旧规则；IP 地址是单行。
	if (text.find('\n') != std::string::npos ||
			text.find(' ') != std::string::npos) {
		renderOverlayText(text, rgb, surface);
		return;
	}
	surface.clear();
	const int scale = 2;
	const int spacing = 1;
	const int baseline = 13;  // scale2 占 3..12 行
	const int width = measureText(text, scale, spacing);
	if (width <= kScreenWidth) {
		drawText(surface, text, (kScreenWidth - width) / 2, baseline, scale,
				spacing, rgb);
		return;
	}
	const int cycle = width + 8;
	int offset = offsetPx % cycle;
	if (offset < 0) {
		offset += cycle;
	}
	drawText(surface, text, -offset, baseline, scale, spacing, rgb);
	const int secondX = cycle - offset;
	if (secondX < kScreenWidth) {
		drawText(surface, text, secondX, baseline, scale, spacing, rgb);
	}
}

}  // namespace PageRenderer
