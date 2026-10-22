// v3 页面渲染器（契约 §3）：纯函数层，只依赖 Surface + TomThumb + TimeMath。
//
// 设计约束：
// - 不 include 任何 FlyThings / SDK 头；不读全局时间（now 由调用方注入）；
// - 每帧只接收一个 `now` 与一次 `TimerEngine::evaluate()` 的结果，保证分屏上下
//   两半、时钟与倒计时严格同秒（契约 §1/§3）；
// - 所有绘制先 `clear()`（黑底），输出整帧 52x16。
//
// PageHost 可复用：
//   PageRenderer::measureText / drawText / drawTextCentered / drawScaledText /
//   truncateToWidth / renderOverlayText / renderPageBanner
#ifndef PAGES_PAGERENDERER_H_
#define PAGES_PAGERENDERER_H_

#include <stdint.h>

#include <string>

#include "live/LiveTypes.h"
#include "pages/PageTypes.h"
#include "timer/TimerEngine.h"
#include "timer/TimerTypes.h"
#include "utils/Surface.h"

namespace PageRenderer {

// 屏幕与几何常量（契约 §3 / 设计 §2.2）
const int kScreenWidth = 52;
const int kScreenHeight = 16;
const int kTopBaseline = 7;      // 分屏上半基线：字形占 2..6 行
const int kBottomBaseline = 14;  // 分屏下半基线：字形占 9..13 行

// live 页：上下两行、每行一个直播间（契约 §3，行高 8px，基线 6/14）
const int kLiveRowCount = 2;
const int kLiveRowHeight = 8;
const int kLiveTopBaseline = 6;     // 上行基线：字形占 1..5 行
const int kLiveBottomBaseline = 14;  // 下行基线：字形占 9..13 行
const int kLiveTextLeft = 1;        // 左侧留 1px（x=0 恒黑）
const int kLiveTextRight = 51;      // 右侧顶到最后一列（x=51）
const int kLiveTextWidth = 51;      // 文本区像素预算 = x 1..51

const int kColorTimeInvalid = 0x404040;  // 时间未同步的暗灰占位（v2 语义）
const int kLiveValueColor = 0xFFFFFF;    // 直播数值固定白色（契约 §3）

// ---- TomThumb 文本度量/绘制（与 v2 TimerPage 完全相同的可见宽度算法）----
int glyphVisibleWidth(char c);
int charAdvance(char c, int scale);
int measureText(const std::string& text, int scale, int spacing);
void drawText(Surface& surface, const std::string& text, int x, int baseline,
		int scale, int spacing, uint32_t rgb);
void drawTextCentered(Surface& surface, const std::string& text, int baseline,
		int scale, int spacing, uint32_t rgb);

// v2 drawScaledText 语义：全屏居中；超宽时先收紧 spacing，仍超宽按 52 截断。
// 基线 = (16 - 5*scale)/2 + 5*scale（scale=2 时基线 13、占 3..12 行）。
void drawScaledText(Surface& surface, const std::string& text, int scale,
		int spacing, uint32_t rgb);

// 把文本从尾部逐字符截断到 <= maxWidth 像素（scale=1、spacing=1 的度量）。
std::string truncateToWidth(const std::string& text, int maxWidth);
// 从 x 开始画截断后的文本（scale=1、spacing=1）。
void drawTextFitted(Surface& surface, const std::string& text, int x,
		int width, int baseline, uint32_t rgb);

// live 行 `<label><gap><value>` 的布局（fit 阶梯，设计 §2.4.4）：
//   (gap2,label3) (gap1,label3) (gap2,label2) (gap1,label2) (gap2,label1) (gap1,label1)
// 51px 预算下典型组合必然收敛；数值永不截断，label 只截断不舍入。
// fits=false 时仍返回最后的兜底布局。
struct LiveCellLayout {
	std::string label;   // 截断后的 label（1..3 字符）
	int gap;             // 2 或 1
	int labelWidth;
	int valueWidth;
	int labelX;
	int valueX;
	bool fits;

	LiveCellLayout();
};

// rowIndex 0=上行 1=下行；越界返回 false。
bool layoutLiveCell(const std::string& label, const std::string& value,
		int rowIndex, LiveCellLayout& out);

// 渲染一整帧。pageIndex 越界 / pages 为空返回 false（surface 已清黑）。
// live 仅在 live 页使用，其它页可传空快照。
bool renderPage(const TimerConfig& config, int pageIndex, long long nowUtc,
		const Stage& stage, bool timeValid, const LiveSnapshot& live,
		Surface& surface);

// 分屏时间行：在 52px 内尽量用大字距（3→0），放不下时返回最小 spacing（调用方
// 仍超宽则应截断）。测试与渲染共用，保证 expected 帧一致。
int timeSpacingForWidth(const std::string& text, int maxSpacing = 3);

// 切页横幅：上行 shortTitle（ASCII，超宽截断），下行 "n/N"；基线 7/14，scale 1。
void renderPageBanner(int index, int count, const std::string& shortTitle,
		const TimerConfig& config, Surface& surface);

// 覆盖层（IP / NO WIFI / AP ...）：沿用 v2 的两行规则，整屏只显示覆盖文本。
void renderOverlayText(const std::string& text, uint32_t rgb,
		Surface& surface);

// 单行覆盖层（IP 地址）：scale 2、整屏高度居中；宽度超过 52px 时按
// offsetPx 水平滚动（offset 从 0 到 width+8 循环）。含空格/换行的文本
// 仍交给 renderOverlayText。
void renderOverlayTextScrolled(const std::string& text, uint32_t rgb,
		int offsetPx, Surface& surface);

}  // namespace PageRenderer

#endif  // PAGES_PAGERENDERER_H_
