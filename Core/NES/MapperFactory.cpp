#include "pch.h"
#include "NES/MapperFactory.h"
#include "NES/NesConsole.h"
#include "NES/Loaders/RomLoader.h"
#include "NES/Loaders/UnifBoards.h"
#include "NES/BaseMapper.h"
#include "NES/RomData.h"
#include "Utilities/VirtualFile.h"
#include "Shared/MessageManager.h"

// Only include Namco 163
#include "NES/Mappers/Namco/Namco163.h"

BaseMapper* MapperFactory::GetMapperFromID(RomData &romData)
{
	switch(romData.Info.MapperID) {
		case 19: return new Namco163();
		case 210: return new Namco163();
	}

	if(romData.Info.MapperID != UnifBoards::UnknownBoard) {
		MessageManager::DisplayMessage("Error", "UnsupportedMapper", "iNES #" + std::to_string(romData.Info.MapperID));
	}
	return nullptr;
}

unique_ptr<BaseMapper> MapperFactory::InitializeFromFile(NesConsole* console, VirtualFile &romFile, RomData &romData, LoadRomResult& result)
{
	romData = {};
	bool databaseEnabled = !console->GetNesConfig().DisableGameDatabase;
	if(RomLoader::LoadFile(romFile, romData, databaseEnabled)) {
		unique_ptr<BaseMapper> mapper(GetMapperFromID(romData));
		if(mapper) {
			result = LoadRomResult::Success;
			mapper->Initialize(console, romData);
			return mapper;
		} else {
			//File is a valid NES file, but it couldn't be loaded
			result = LoadRomResult::Failure;
			return nullptr;
		}
	}
	result = LoadRomResult::UnknownType;
	return nullptr;
}

