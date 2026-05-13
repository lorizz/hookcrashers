#pragma once

#include <cstdint>
#include <string_view>

namespace HookCrashers::SWF {
	enum class SWFTagCode : uint16_t {
		End = 0,
		ShowFrame = 1,
		DefineShape = 2,
		PlaceObject = 4,
		RemoveObject = 5,
		DefineBits = 6,
		DefineButton = 7,
		JPEGTables = 8,
		SetBackgroundColor = 9,
		DefineFont = 10,
		DefineText = 11,
		DoAction = 12,
		DefineFontInfo = 13,
		DefineSound = 14,
		StartSound = 15,
		DefineButtonSound = 17,
		SoundStreamHead = 18,
		SoundStreamBlock = 19,
		DefineBitsLossless = 20,
		DefineBitsJPEG2 = 21,
		DefineShape2 = 22,
		DefineButtonCxform = 23,
		Protect = 24,
		PlaceObject2 = 26,
		RemoveObject2 = 28,
		DefineShape3 = 32,
		DefineText2 = 33,
		DefineButton2 = 34,
		DefineBitsJPEG3 = 35,
		DefineBitsLossless2 = 36,
		DefineEditText = 37,
		DefineSprite = 39,
		FrameLabel = 43,
		SoundStreamHead2 = 45,
		DefineMorphShape = 46,
		DefineFont2 = 48,
		ExportAssets = 56,
		ImportAssets = 57,
		EnableDebugger = 58,
		DoInitAction = 59,
		DefineVideoStream = 60,
		VideoFrame = 61,
		DefineFontInfo2 = 62,
		EnableDebugger2 = 64,
		ScriptLimits = 65,
		SetTabIndex = 66,
		FileAttributes = 69,
		PlaceObject3 = 70,
		ImportAssets2 = 71,
		DefineFontAlignZones = 73,
		CSMTextSettings = 74,
		DefineFont3 = 75,
		SymbolClass = 76,
		Metadata = 77,
		DefineScalingGrid = 78,
		DoABC = 82,
		DefineShape4 = 83,
		DefineMorphShape2 = 84,
		DefineSceneAndFrameLabelData = 86,
		DefineBinaryData = 87,
		DefineFontName = 88,
		StartSound2 = 89,
		DefineBitsJPEG4 = 90,
		DefineFont4 = 91,
	};

	enum class AS2ActionCode : uint8_t {
		End = 0x00,
		NextFrame = 0x04,
		PreviousFrame = 0x05,
		Play = 0x06,
		Stop = 0x07,
		ToggleQuality = 0x08,
		StopSounds = 0x09,
		Add = 0x0A,
		Subtract = 0x0B,
		Multiply = 0x0C,
		Divide = 0x0D,
		Equals = 0x0E,
		Less = 0x0F,
		And = 0x10,
		Or = 0x11,
		Not = 0x12,
		StringEquals = 0x13,
		StringLength = 0x14,
		StringExtract = 0x15,
		Pop = 0x17,
		ToInteger = 0x18,
		GetVariable = 0x1C,
		SetVariable = 0x1D,
		SetTarget2 = 0x20,
		StringAdd = 0x21,
		GetProperty = 0x22,
		SetProperty = 0x23,
		CloneSprite = 0x24,
		RemoveSprite = 0x25,
		Trace = 0x26,
		StartDrag = 0x27,
		EndDrag = 0x28,
		StringLess = 0x29,
		Throw = 0x2A,
		CastOp = 0x2B,
		ImplementsOp = 0x2C,
		RandomNumber = 0x30,
		MBStringLength = 0x31,
		CharToAscii = 0x32,
		AsciiToChar = 0x33,
		GetTime = 0x34,
		MBStringExtract = 0x35,
		MBCharToAscii = 0x36,
		MBAsciiToChar = 0x37,
		Delete = 0x3A,
		Delete2 = 0x3B,
		DefineLocal = 0x3C,
		CallFunction = 0x3D,
		Return = 0x3E,
		Modulo = 0x3F,
		NewObject = 0x40,
		DefineLocal2 = 0x41,
		InitArray = 0x42,
		InitObject = 0x43,
		TypeOf = 0x44,
		TargetPath = 0x45,
		Enumerate = 0x46,
		Add2 = 0x47,
		Less2 = 0x48,
		Equals2 = 0x49,
		ToNumber = 0x4A,
		ToString = 0x4B,
		PushDuplicate = 0x4C,
		StackSwap = 0x4D,
		GetMember = 0x4E,
		SetMember = 0x4F,
		Increment = 0x50,
		Decrement = 0x51,
		CallMethod = 0x52,
		NewMethod = 0x53,
		InstanceOf = 0x54,
		Enumerate2 = 0x55,
		BitAnd = 0x60,
		BitOr = 0x61,
		BitXor = 0x62,
		BitLShift = 0x63,
		BitRShift = 0x64,
		BitURShift = 0x65,
		StrictEquals = 0x66,
		Greater = 0x67,
		StringGreater = 0x68,
		Extends = 0x69,
		GotoFrame = 0x81,
		GetURL = 0x83,
		StoreRegister = 0x87,
		ConstantPool = 0x88,
		WaitForFrame = 0x8A,
		SetTarget = 0x8B,
		GoToLabel = 0x8C,
		WaitForFrame2 = 0x8D,
		DefineFunction2 = 0x8E,
		Try = 0x8F,
		With = 0x94,
		Push = 0x96,
		Jump = 0x99,
		GetURL2 = 0x9A,
		DefineFunction = 0x9B,
		If = 0x9D,
		Call = 0x9E,
		GotoFrame2 = 0x9F,
	};

	struct SWFTagHeader {
		uint16_t code = 0;
		uint32_t length = 0;
		uint8_t* payload = nullptr;
		uint8_t* next = nullptr;
	};

	inline uint16_t ReadU16LE(const uint8_t* data) {
		return static_cast<uint16_t>(data[0] | (data[1] << 8));
	}

	inline uint32_t ReadU32LE(const uint8_t* data) {
		return static_cast<uint32_t>(data[0])
			| (static_cast<uint32_t>(data[1]) << 8)
			| (static_cast<uint32_t>(data[2]) << 16)
			| (static_cast<uint32_t>(data[3]) << 24);
	}

	inline bool ReadSWFTagHeader(uint8_t* cursor, uint8_t* end, SWFTagHeader& out) {
		if (!cursor || cursor + 2 > end) {
			return false;
		}

		const uint16_t raw = ReadU16LE(cursor);
		out.code = raw >> 6;
		out.length = raw & 0x3F;
		cursor += 2;
		if (out.length == 0x3F) {
			if (cursor + 4 > end) {
				return false;
			}
			out.length = ReadU32LE(cursor);
			cursor += 4;
		}
		if (cursor + out.length > end) {
			return false;
		}
		out.payload = cursor;
		out.next = cursor + out.length;
		return true;
	}

	inline std::string_view LookupSWFTagName(uint16_t tagCode) {
		switch (static_cast<SWFTagCode>(tagCode)) {
		case SWFTagCode::End: return "End";
		case SWFTagCode::ShowFrame: return "ShowFrame";
		case SWFTagCode::DoAction: return "DoAction";
		case SWFTagCode::DefineSprite: return "DefineSprite";
		case SWFTagCode::DoInitAction: return "DoInitAction";
		case SWFTagCode::SymbolClass: return "SymbolClass";
		case SWFTagCode::ExportAssets: return "ExportAssets";
		case SWFTagCode::FrameLabel: return "FrameLabel";
		default: return {};
		}
	}

	inline std::string_view LookupAS2ActionName(uint8_t actionCode) {
		switch (static_cast<AS2ActionCode>(actionCode)) {
		case AS2ActionCode::End: return "End";
		case AS2ActionCode::ConstantPool: return "ConstantPool";
		case AS2ActionCode::Push: return "Push";
		case AS2ActionCode::CallFunction: return "CallFunction";
		case AS2ActionCode::CallMethod: return "CallMethod";
		case AS2ActionCode::GetVariable: return "GetVariable";
		case AS2ActionCode::SetVariable: return "SetVariable";
		case AS2ActionCode::GetMember: return "GetMember";
		case AS2ActionCode::SetMember: return "SetMember";
		case AS2ActionCode::DefineFunction: return "DefineFunction";
		case AS2ActionCode::DefineFunction2: return "DefineFunction2";
		case AS2ActionCode::If: return "If";
		case AS2ActionCode::Jump: return "Jump";
		case AS2ActionCode::GotoFrame: return "GotoFrame";
		case AS2ActionCode::GotoFrame2: return "GotoFrame2";
		case AS2ActionCode::GoToLabel: return "GoToLabel";
		case AS2ActionCode::Play: return "Play";
		case AS2ActionCode::Stop: return "Stop";
		case AS2ActionCode::Return: return "Return";
		case AS2ActionCode::Pop: return "Pop";
		default: return {};
		}
	}
}
