// 直播数据快照（纯 C++11，POD + 少量内联方法，不依赖平台 SDK）。
//
// 职责边界：
// - LiveService / LiveCache（device-v3）负责取数、判定状态，填充本文件的结构；
// - PageRenderer 只读快照，不做网络、不看时间。
//
// 一页固定 2 个房间（契约 §3：上下两行，每行一个直播间）。槽位与
// `PageConfig::rooms` 的下标一一对应：rooms[0] 是上行，rooms[1] 是下行。
#ifndef LIVE_LIVETYPES_H_
#define LIVE_LIVETYPES_H_

#include <string>

#include "pages/PageTypes.h"

// 一页固定 2 个房间（契约 §2/§3）。
const int kLiveCellCount = 2;

// 单元格数据状态（契约 §3 / 设计 §2.4.9）。
enum LiveRoomState {
	LIVE_ROOM_EMPTY = 0,  // 未配置 / disabled：留黑，不轮询
	LIVE_ROOM_WAIT = 1,   // 已配置、等待首次响应：`--` dim
	LIVE_ROOM_OK = 2,     // 有新鲜数据：数值白
	LIVE_ROOM_STALE = 3,  // 有旧值但超过 3xrefreshSec：旧值 dim
	LIVE_ROOM_ERROR = 4   // 失败且无可用旧值：`--` dim
};

// 单个指标值。`valid=false` 与数值 0 严格区分（契约 §5：0 是有效值）。
struct LiveMetricValue {
	bool valid;
	long long value;

	LiveMetricValue() : valid(false), value(0) {
	}

	explicit LiveMetricValue(long long v) : valid(true), value(v) {
	}

	void set(long long v) {
		valid = true;
		value = v;
	}

	void clear() {
		valid = false;
		value = 0;
	}
};

// 一个直播行（房间）的数据。数值字段由 LiveService 填；未配置的槽位保持默认。
struct LiveRoomData {
	LiveRoomState state;
	LiveMetricValue online;
	LiveMetricValue likes;
	LiveMetricValue fans;
	LiveMetricValue liveStatus;  // 0=未开播 1=直播中（契约 status）
	long long updatedUtc;        // 最近成功时间（诊断用；渲染不看它）
	std::string error;           // 最近错误文本（诊断用）

	LiveRoomData() : state(LIVE_ROOM_EMPTY), updatedUtc(0) {
	}

	void clear() {
		state = LIVE_ROOM_EMPTY;
		online.clear();
		likes.clear();
		fans.clear();
		liveStatus.clear();
		updatedUtc = 0;
		error.clear();
	}

	// 是否带任意可用值（用于“整页是否有数据”的判定）。
	bool hasAnyValue() const {
		return online.valid || likes.valid || fans.valid || liveStatus.valid;
	}
};

// 当前页的 2 行快照。纯值语义，可整份拷贝。
struct LiveSnapshot {
	LiveRoomData rooms[kLiveCellCount];

	LiveSnapshot() {
	}

	void clear() {
		for (int i = 0; i < kLiveCellCount; ++i) {
			rooms[i].clear();
		}
	}

	// 有任意一格带值（任何指标）。
	bool hasAnyValue() const {
		for (int i = 0; i < kLiveCellCount; ++i) {
			if (rooms[i].hasAnyValue()) {
				return true;
			}
		}
		return false;
	}
};

#endif  // LIVE_LIVETYPES_H_
