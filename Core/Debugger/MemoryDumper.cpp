#include "pch.h"
#include "Debugger/MemoryDumper.h"
#include "Shared/Emulator.h"
#include "Debugger/Debugger.h"
#include "Debugger/DebugTypes.h"
#include "Debugger/DebugUtilities.h"
#include "Shared/BaseControlManager.h"
#include "Debugger/CodeDataLogger.h"
#include "Debugger/CdlManager.h"
#include "Debugger/Disassembler.h"
#include "Debugger/DebugBreakHelper.h"
#include "Shared/Interfaces/IConsole.h"
#include "NES/NesConsole.h"

MemoryDumper::MemoryDumper(Debugger* debugger)
{
	_debugger = debugger;
	_emu = debugger->GetEmulator();

	IConsole* console = _debugger->GetConsole();
	if(NesConsole* nes = dynamic_cast<NesConsole*>(console)) {
		_nesConsole = nes;
	}

	for(int i = 0; i < DebugUtilities::GetMemoryTypeCount(); i++) {
		MemoryType memType = (MemoryType)i;
		if(memType != MemoryType::None) {
			_isMemorySupported[i] = _emu->GetMemory(memType).Memory != nullptr || _debugger->HasCpuType(DebugUtilities::ToCpuType(memType));
		}
	}
}

uint8_t* MemoryDumper::GetMemoryBuffer(MemoryType type)
{
	return (uint8_t*)_emu->GetMemory(type).Memory;
}

uint32_t MemoryDumper::GetMemorySize(MemoryType type)
{
	if(!_isMemorySupported[(int)type]) {
		return 0;
	}

	switch(type) {
		case MemoryType::NesMemory: return 0x10000;
		case MemoryType::NesPpuMemory: return 0x4000;
		default: return _emu->GetMemory(type).Size;
	}
}

void MemoryDumper::GetMemoryState(MemoryType type, uint8_t *buffer)
{
	if(GetMemorySize(type) == 0) {
		return;
	}

	switch(type) {
		case MemoryType::NesMemory: {
			if(_nesConsole) {
				for(int i = 0; i <= 0xFFFF; i++) {
					buffer[i] = _nesConsole->DebugRead(i);
				}
			}
			break;
		}

		case MemoryType::NesPpuMemory: {
			if(_nesConsole) {
				for(int i = 0; i < 0x4000; i++) {
					buffer[i] = _nesConsole->DebugReadVram(i);
				}
			}
			break;
		}

		default: 
			uint8_t* src = GetMemoryBuffer(type);
			if(src) {
				memcpy(buffer, src, GetMemorySize(type));
			}
			break;
	}
}

void MemoryDumper::InternalSetMemoryValues(MemoryType memoryType, uint32_t startAddress, uint8_t* data, uint32_t length, bool disableSideEffects, bool undoAllowed)
{
	UndoBatch undoBatch;
	UndoEntry undoEntry = {};
	
	if(undoAllowed) {
		undoEntry.MemType = memoryType;
		undoEntry.StartAddress = startAddress;
	}

	Disassembler* disassembler = _debugger->GetDisassembler();

	for(uint32_t i = 0; i < length; i++) {
		uint32_t address = startAddress + i;
		uint8_t value = data[i];

		switch(memoryType) {
			case MemoryType::NesMemory:
				if(_nesConsole) {
					if(undoAllowed) undoEntry.OriginalData.push_back(_nesConsole->DebugRead(address));
					_nesConsole->DebugWrite(address, value, disableSideEffects);
				}
				break;
			case MemoryType::NesPpuMemory:
				if(_nesConsole) {
					if(undoAllowed) undoEntry.OriginalData.push_back(_nesConsole->DebugReadVram(address));
					_nesConsole->DebugWriteVram(address, value);
				}
				break;

			default:
				uint8_t* src = GetMemoryBuffer(memoryType);
				if(src) {
					if(undoAllowed) {
						if(address >= GetMemorySize(memoryType)) {
							continue;
						}
						undoEntry.OriginalData.push_back(src[address]);
					}
					
					switch(memoryType) {
						case MemoryType::NesSpriteRam: 
						case MemoryType::NesSecondarySpriteRam: 
							src[address] = (address & 0x03) == 0x02 ? (value & 0xE3) : value; 
							break;
						case MemoryType::NesPaletteRam: 
							src[address] = value & 0x3F; 
							break;
						default:
							src[address] = value;
							AddressInfo addr = { (int32_t)address, memoryType };
							disassembler->InvalidateCache(addr, DebugUtilities::ToCpuType(memoryType));
							break;
					}
				}
				break;
		}
	}

	if(undoAllowed && undoEntry.MemType != MemoryType::None) {
		undoBatch.Entries.push_back(undoEntry);
		auto lock = _undoLock.AcquireSafe();
		_undoHistory.push_back(undoBatch);
		if(_undoHistory.size() > 200) {
			_undoHistory.pop_front();
		}
	}
}

void MemoryDumper::SetMemoryValues(MemoryType memoryType, uint32_t address, uint8_t* data, uint32_t length)
{
	DebugBreakHelper helper(_debugger);
	InternalSetMemoryValues(memoryType, address, data, length, true, true);
}

void MemoryDumper::SetMemoryValue(MemoryType memoryType, uint32_t address, uint8_t value, bool disableSideEffects)
{
	InternalSetMemoryValues(memoryType, address, &value, 1, disableSideEffects, true);
}

void MemoryDumper::GetMemoryValues(MemoryType memoryType, uint32_t start, uint32_t end, uint8_t* output)
{
	int x = 0;
	uint32_t size = GetMemorySize(memoryType);
	for(uint32_t i = start; i <= end && i < size; i++) {
		output[x++] = InternalGetMemoryValue(memoryType, i);
	}

	if(end >= size) {
		memset(output + x, 0, end - start - x + 1);
	}
}

uint8_t MemoryDumper::GetMemoryValue(MemoryType memoryType, uint32_t address, bool disableSideEffects)
{
	if(address >= GetMemorySize(memoryType)) {
		return 0;
	}
	return InternalGetMemoryValue(memoryType, address, disableSideEffects);
}

uint8_t MemoryDumper::InternalGetMemoryValue(MemoryType memoryType, uint32_t address, bool disableSideEffects)
{
	switch(memoryType) {
		case MemoryType::NesMemory: return _nesConsole ? _nesConsole->DebugRead(address) : 0;
		case MemoryType::NesPpuMemory: return _nesConsole ? _nesConsole->DebugReadVram(address) : 0;
		default:
			uint8_t* src = GetMemoryBuffer(memoryType);
			return src ? src[address] : 0;
	}
}

uint16_t MemoryDumper::GetMemoryValue16(MemoryType memoryType, uint32_t address, bool disableSideEffects)
{
	uint32_t memorySize = GetMemorySize(memoryType);
	uint8_t lsb = GetMemoryValue(memoryType, address);
	uint8_t msb = GetMemoryValue(memoryType, address + 1 >= memorySize ? 0 : address + 1);
	return (msb << 8) | lsb;
}

uint32_t MemoryDumper::GetMemoryValue32(MemoryType memoryType, uint32_t address, bool disableSideEffects)
{
	uint32_t memorySize = GetMemorySize(memoryType);
	uint8_t b0 = GetMemoryValue(memoryType, address);
	uint8_t b1 = GetMemoryValue(memoryType, address + 1 >= memorySize ? 0 : address + 1);
	uint8_t b2 = GetMemoryValue(memoryType, address + 2 >= memorySize ? 0 : address + 2);
	uint8_t b3 = GetMemoryValue(memoryType, address + 3 >= memorySize ? 0 : address + 3);
	return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
}

void MemoryDumper::SetMemoryValue16(MemoryType memoryType, uint32_t address, uint16_t value, bool disableSideEffects)
{
	DebugBreakHelper helper(_debugger);
	SetMemoryValue(memoryType, address, (uint8_t)value, disableSideEffects);
	SetMemoryValue(memoryType, address + 1, (uint8_t)(value >> 8), disableSideEffects);
}

void MemoryDumper::SetMemoryValue32(MemoryType memoryType, uint32_t address, uint32_t value, bool disableSideEffects)
{
	DebugBreakHelper helper(_debugger);
	SetMemoryValue(memoryType, address, (uint8_t)value, disableSideEffects);
	SetMemoryValue(memoryType, address + 1, (uint8_t)(value >> 8), disableSideEffects);
	SetMemoryValue(memoryType, address + 2, (uint8_t)(value >> 16), disableSideEffects);
	SetMemoryValue(memoryType, address + 3, (uint8_t)(value >> 24), disableSideEffects);
}

void MemoryDumper::SetMemoryState(MemoryType type, uint8_t *buffer, uint32_t length)
{
	InternalSetMemoryValues(type, 0, buffer, length, true, false);
}

bool MemoryDumper::HasUndoHistory()
{
	auto lock = _undoLock.AcquireSafe();
	return _undoHistory.size() > 0;
}

void MemoryDumper::PerformUndo()
{
	auto lock = _undoLock.AcquireSafe();
	if(!_undoHistory.empty()) {
		DebugBreakHelper helper(_debugger);
		UndoBatch& batch = _undoHistory.back();
		for(auto entry : batch.Entries) {
			InternalSetMemoryValues(entry.MemType, entry.StartAddress, entry.OriginalData.data(), (uint32_t)entry.OriginalData.size(), true, false);
		}
		_undoHistory.pop_back();
		_debugger->GetCdlManager()->RefreshCodeCache();
	}
}
