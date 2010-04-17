/* Copyright (C) 2009 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "precompiled.h"

#include "Camera.h"
#include "CinemaTrack.h"
#include "LightEnv.h"
#include "MapReader.h"
#include "MapWriter.h"
#include "Model.h"
#include "ObjectBase.h"
#include "ObjectEntry.h"
#include "ObjectManager.h"
#include "Patch.h"
#include "Terrain.h"
#include "TextureEntry.h"
#include "TextureManager.h"
#include "Unit.h"
#include "UnitManager.h"

#include "maths/MathUtil.h"
#include "maths/NUSpline.h"
#include "ps/Loader.h"
#include "ps/Player.h"
#include "ps/Filesystem.h"
#include "ps/XML/XMLWriter.h"
#include "renderer/SkyManager.h"
#include "renderer/WaterManager.h"
#include "simulation/EntityTemplate.h"
#include "simulation/EntityTemplateCollection.h"
#include "simulation/TriggerManager.h"
#include "simulation/Entity.h"
#include "simulation2/Simulation2.h"
#include "simulation2/components/ICmpOwnership.h"
#include "simulation2/components/ICmpPosition.h"
#include "simulation2/components/ICmpTemplateManager.h"

#define CURRENT_FILE_VERSION (g_UseSimulation2 ? FILE_VERSION : 4)

///////////////////////////////////////////////////////////////////////////////////////////////////
// CMapWriter constructor: nothing to do at the minute
CMapWriter::CMapWriter()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// SaveMap: try to save the current map to the given file
void CMapWriter::SaveMap(const VfsPath& pathname, CTerrain* pTerrain,
						 CUnitManager* pUnitMan, WaterManager* pWaterMan, SkyManager* pSkyMan,
						 CLightEnv* pLightEnv, CCamera* pCamera, CCinemaManager* pCinema)
{
	CFilePacker packer(CURRENT_FILE_VERSION, "PSMP");

	// build necessary data
	PackMap(packer, pTerrain);

	// write it out
	packer.Write(pathname);

	VfsPath pathnameXML = fs::change_extension(pathname, L".xml");
	WriteXML(pathnameXML, pUnitMan, pWaterMan, pSkyMan, pLightEnv, pCamera, pCinema);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// GetHandleIndex: return the index of the given handle in the given list; or 0xFFFF if
// handle isn't in list
static u16 GetHandleIndex(const Handle handle, const std::vector<Handle>& handles)
{
	const size_t limit = std::min(handles.size(), size_t(0xFFFEu));	// paranoia
	for (size_t i=0;i<limit;i++) {
		if (handles[i]==handle) {
			return (u16)i;
		}
	}

	return 0xFFFF;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// EnumTerrainTextures: build lists of textures used by map, and tile descriptions for 
// each tile on the terrain
void CMapWriter::EnumTerrainTextures(CTerrain *pTerrain,
									 std::vector<CStr>& textures,
									 std::vector<STileDesc>& tiles)
{
	// the list of all handles in use
	std::vector<Handle> handles;
	
	// resize tile array to required size
	tiles.resize(SQR(pTerrain->GetVerticesPerSide()-1));
	STileDesc* tileptr=&tiles[0];

	// now iterate through all the tiles
	const ssize_t patchesPerSide=pTerrain->GetPatchesPerSide();
	for (ssize_t j=0;j<patchesPerSide;j++) {
		for (ssize_t i=0;i<patchesPerSide;i++) {
			for (ssize_t m=0;m<PATCH_SIZE;m++) {
				for (ssize_t k=0;k<PATCH_SIZE;k++) {
					CMiniPatch& mp=pTerrain->GetPatch(i,j)->m_MiniPatches[m][k];	// can't fail
					u16 index=u16(GetHandleIndex(mp.Tex1,handles));
					if (index==0xFFFF) {
						index=(u16)handles.size();
						handles.push_back(mp.Tex1);
					}

					tileptr->m_Tex1Index=index;
					tileptr->m_Tex2Index=0xFFFF;
					tileptr->m_Priority=mp.Tex1Priority;
					tileptr++;
				}
			}
		}
	}

	// now find the texture names for each handle
	for (size_t i=0;i<handles.size();i++) {
		CStr texturename;
		CTextureEntry* texentry=g_TexMan.FindTexture(handles[i]);
		if (!texentry) {
			// uh-oh, this shouldn't happen; set texturename to empty string
			texturename="";
		} else {
			texturename=texentry->GetTag();
		}
		textures.push_back(texturename);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PackMap: pack the current world into a raw data stream
void CMapWriter::PackMap(CFilePacker& packer, CTerrain* pTerrain)
{
	// now pack everything up
	PackTerrain(packer, pTerrain);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PackTerrain: pack the terrain onto the end of the output data stream
//		- data: map size, heightmap, list of textures used by map, texture tile assignments
void CMapWriter::PackTerrain(CFilePacker& packer, CTerrain* pTerrain)
{
	// pack map size
	const ssize_t mapsize = pTerrain->GetPatchesPerSide();
	packer.PackSize(mapsize);
	
	// pack heightmap
	packer.PackRaw(pTerrain->GetHeightMap(),sizeof(u16)*SQR(pTerrain->GetVerticesPerSide()));	

	// the list of textures used by map
	std::vector<CStr> terrainTextures;
	// descriptions of each tile
	std::vector<STileDesc> tiles;
	
	// build lists by scanning through the terrain
	EnumTerrainTextures(pTerrain, terrainTextures, tiles);
	
	// pack texture names
	const size_t numTextures = terrainTextures.size();
	packer.PackSize(numTextures);
	for (size_t i=0;i<numTextures;i++)
		packer.PackString(terrainTextures[i]);
	
	// pack tile data
	packer.PackRaw(&tiles[0],sizeof(STileDesc)*tiles.size());	
}
void CMapWriter::WriteXML(const VfsPath& filename,
						  CUnitManager* pUnitMan, WaterManager* pWaterMan, SkyManager* pSkyMan,
						  CLightEnv* pLightEnv, CCamera* pCamera, CCinemaManager* pCinema)
{
	XML_Start();

	{
		XML_Element("Scenario");
		XML_Attribute("version", (int)CURRENT_FILE_VERSION);

		{
			XML_Element("Environment");

			XML_Setting("SkySet", pSkyMan->GetSkySet());
			{
				XML_Element("SunColour");
				XML_Attribute("r", pLightEnv->m_SunColor.X); // yes, it's X/Y/Z...
				XML_Attribute("g", pLightEnv->m_SunColor.Y);
				XML_Attribute("b", pLightEnv->m_SunColor.Z);
			}
			{
				XML_Element("SunElevation");
				XML_Attribute("angle", pLightEnv->m_Elevation);
			}
			{
				XML_Element("SunRotation");
				XML_Attribute("angle", pLightEnv->m_Rotation);
			}
			{
				XML_Element("TerrainAmbientColour");
				XML_Attribute("r", pLightEnv->m_TerrainAmbientColor.X);
				XML_Attribute("g", pLightEnv->m_TerrainAmbientColor.Y);
				XML_Attribute("b", pLightEnv->m_TerrainAmbientColor.Z);
			}
			{
				XML_Element("UnitsAmbientColour");
				XML_Attribute("r", pLightEnv->m_UnitsAmbientColor.X);
				XML_Attribute("g", pLightEnv->m_UnitsAmbientColor.Y);
				XML_Attribute("b", pLightEnv->m_UnitsAmbientColor.Z);
			}

			{
				XML_Element("Water");
				{
					XML_Element("WaterBody");
					XML_Setting("Type", "default");
					{
						XML_Element("Colour");
						XML_Attribute("r", pWaterMan->m_WaterColor.r);
						XML_Attribute("g", pWaterMan->m_WaterColor.g);
						XML_Attribute("b", pWaterMan->m_WaterColor.b);
					}
					XML_Setting("Height", pWaterMan->m_WaterHeight);
					XML_Setting("Shininess", pWaterMan->m_Shininess);
					XML_Setting("Waviness", pWaterMan->m_Waviness);
					XML_Setting("Murkiness", pWaterMan->m_Murkiness);
					{
						XML_Element("Tint");
						XML_Attribute("r", pWaterMan->m_WaterTint.r);
						XML_Attribute("g", pWaterMan->m_WaterTint.g);
						XML_Attribute("b", pWaterMan->m_WaterTint.b);
					}
					{
						XML_Element("ReflectionTint");
						XML_Attribute("r", pWaterMan->m_ReflectionTint.r);
						XML_Attribute("g", pWaterMan->m_ReflectionTint.g);
						XML_Attribute("b", pWaterMan->m_ReflectionTint.b);
					}
					XML_Setting("ReflectionTintStrength", pWaterMan->m_ReflectionTintStrength);
				}
			}
		}

		{
			XML_Element("Camera");

			{
				XML_Element("Position");
				CVector3D pos = pCamera->m_Orientation.GetTranslation();
				XML_Attribute("x", pos.X);
				XML_Attribute("y", pos.Y);
				XML_Attribute("z", pos.Z);
			}

			CVector3D in = pCamera->m_Orientation.GetIn();
			// Convert to spherical coordinates
			float rotation = atan2(in.X, in.Z);
			float declination = atan2(sqrt(in.X*in.X + in.Z*in.Z), in.Y) - (float)M_PI/2;

			{
				XML_Element("Rotation");
				XML_Attribute("angle", rotation);
			}
			{
				XML_Element("Declination");
				XML_Attribute("angle", declination);
			}
		}

		const std::vector<CUnit*>& units = pUnitMan->GetUnits();

		if (g_UseSimulation2)
		{
			XML_Element("Entities");

			CSimulation2& sim = *g_Game->GetSimulation2();

			CmpPtr<ICmpTemplateManager> cmpTemplateManager(sim, SYSTEM_ENTITY);
			debug_assert(!cmpTemplateManager.null());

			// This will probably need to be changed in the future, but for now we'll
			// just save all entities that have a position
			const CSimulation2::InterfaceList& ents = sim.GetEntitiesWithInterface(IID_Position);
			for (CSimulation2::InterfaceList::const_iterator it = ents.begin(); it != ents.end(); ++it)
			{
				entity_id_t ent = it->first;

				// Don't save local entities (placement previews etc)
				if (ENTITY_IS_LOCAL(ent))
					continue;

				XML_Element("Entity");
				XML_Attribute("uid", ent);

				XML_Setting("Template", cmpTemplateManager->GetCurrentTemplateName(ent));

				CmpPtr<ICmpOwnership> cmpOwner(sim, ent);
				if (!cmpOwner.null())
					XML_Setting("Player", (int)cmpOwner->GetOwner());

				CmpPtr<ICmpPosition> cmpPosition(sim, ent);
				if (!cmpPosition.null())
				{
					CFixedVector3D pos = cmpPosition->GetPosition();
					CFixedVector3D rot = cmpPosition->GetRotation();
					{
						XML_Element("Position");
						XML_Attribute("x", pos.X.ToDouble());
						XML_Attribute("z", pos.Z.ToDouble());
						// TODO: height offset etc
					}
					{
						XML_Element("Orientation");
						XML_Attribute("y", rot.Y.ToDouble());
						// TODO: X, Z maybe
					}
				}
			}
		}

		if (!g_UseSimulation2)
		{
			XML_Element("Entities");

			for (std::vector<CUnit*>::const_iterator unit = units.begin(); unit != units.end(); ++unit)
			{

				CEntity* entity = (*unit)->GetEntity();

				// Ignore objects that aren't entities
				if (! entity)
					continue;

				XML_Element("Entity");
				XML_Attribute("uid", (unsigned) (*unit)->GetID());
				XML_Setting("Template", entity->m_base->m_Tag);

				XML_Setting("Player", (unsigned) entity->GetPlayer()->GetPlayerID());

				{
					CVector3D position = entity->m_position;

					XML_Element("Position");
					XML_Attribute("x", position.X);
					XML_Attribute("y", position.Y);
					XML_Attribute("z", position.Z);
				}

				{
					float angle = entity->m_orientation.Y;

					XML_Element("Orientation");
					XML_Attribute("angle", angle);
				}
			}
		}
		if (!g_UseSimulation2)
		{
			XML_Element("Nonentities");

			for (std::vector<CUnit*>::const_iterator unit = units.begin(); unit != units.end(); ++unit) {

				// Ignore objects that are entities
				if ((*unit)->GetEntity())
					continue;

				XML_Element("Nonentity");

				XML_Setting("Actor", (*unit)->GetObject().m_Base->m_Name);

				{
					CVector3D position = (*unit)->GetModel()->GetTransform().GetTranslation();

					XML_Element("Position");
					XML_Attribute("x", position.X);
					XML_Attribute("y", position.Y);
					XML_Attribute("z", position.Z);
				}

				{
					CVector3D orient = (*unit)->GetModel()->GetTransform().GetIn();
					float angle = atan2(-orient.X, -orient.Z);

					XML_Element("Orientation");
					XML_Attribute("angle", angle);
				}
			}
		}

		const std::map<CStrW, CCinemaPath>& paths = pCinema->GetAllPaths();
		std::map<CStrW, CCinemaPath>::const_iterator it = paths.begin();
		
		{
			XML_Element("Paths");

			for ( ; it != paths.end(); it++ )
			{
				CStrW name = it->first;
				float timescale = it->second.GetTimescale();
				
				XML_Element("Path");
				XML_Attribute("name", name);
				XML_Attribute("timescale", timescale);

				const std::vector<SplineData>& nodes = it->second.GetAllNodes();
				const CCinemaData* data = it->second.GetData();
				
				{
					XML_Element("Distortion");
					XML_Attribute("mode", data->m_Mode);
					XML_Attribute("style", data->m_Style);
					XML_Attribute("growth", data->m_Growth);
					XML_Attribute("switch", data->m_Switch);
				}

				for ( ssize_t j=nodes.size()-1; j >= 0; --j )
				{
					XML_Element("Node");
					
					{
						XML_Element("Position");
						XML_Attribute("x", nodes[j].Position.X);
						XML_Attribute("y", nodes[j].Position.Y);
						XML_Attribute("z", nodes[j].Position.Z);
					}

					{
						XML_Element("Rotation");
						XML_Attribute("x", nodes[j].Rotation.X);
						XML_Attribute("y", nodes[j].Rotation.Y);
						XML_Attribute("z", nodes[j].Rotation.Z);
					}

					XML_Setting("Time", nodes[j].Distance);
				}
			}
		}
		
		const std::list<MapTriggerGroup>& groups = g_TriggerManager.GetAllTriggerGroups();
		std::list<MapTriggerGroup> rootChildren;
		std::list<MapTriggerGroup>::const_iterator root = std::find( groups.begin(), groups.end(), L"Triggers" );

		if ( root == groups.end() )
		{
			XML_Element("Triggers");
		}
		else
		{
			std::for_each(rootChildren.begin(), rootChildren.end(), CopyIfRootChild(rootChildren));
		
			XML_Element("Triggers");
			for ( std::list<MapTriggerGroup>::const_iterator it = rootChildren.begin(); 
														it != rootChildren.end(); ++it )
			{
				WriteTriggerGroup(xml_file_, *it, groups);
			}
			for ( std::list<MapTrigger>::const_iterator it = root->triggers.begin();
														it != root->triggers.end(); ++it )
			{
				WriteTrigger(xml_file_, *it);
			}
		}
	}
	if (! XML_StoreVFS(filename))
		debug_assert(0);	// failed to write map XML file
}

void CMapWriter::WriteTriggerGroup(XMLWriter_File& xml_file_, const MapTriggerGroup& group, const std::list<MapTriggerGroup>& groupList)
{
	XML_Element("Group");
	XML_Attribute("name", group.name);

	for ( std::list<CStrW>::const_iterator it = group.childGroups.begin(); 
									it != group.childGroups.end(); ++it )
	{
		//Not very efficient...
		std::list<MapTriggerGroup>::const_iterator it2 = std::find(groupList.begin(), groupList.end(), *it);
		if ( it2 != groupList.end() )
			WriteTriggerGroup(xml_file_, *it2, groupList);
		else
			debug_warn(L"Invalid trigger group ID while writing map");
	}

	for ( std::list<MapTrigger>::const_iterator it = group.triggers.begin(); it != group.triggers.end(); ++it )
	{
		WriteTrigger(xml_file_, *it);
	}	
}

void CMapWriter::WriteTrigger(XMLWriter_File& xml_file_, const MapTrigger& trigger)
{
	XML_Element("Trigger");
	XML_Attribute("name", trigger.name);

	{
		if ( trigger.active )
			XML_Setting("Active", "true");
		else
			XML_Setting("Active", "false");
		XML_Setting("MaxRunCount", trigger.maxRunCount);
		XML_Setting("Delay", trigger.timeValue);
	}
		
	{
		XML_Element("Conditions");
		for ( std::list<MapTriggerCondition>::const_iterator it2 = trigger.conditions.begin();
													it2 != trigger.conditions.end(); ++it2 )
		{
			size_t distance = std::distance( trigger.conditions.begin(), it2 );
			std::set<MapTriggerLogicBlock>::const_iterator logicIter;
			
			if ( ( logicIter = trigger.logicBlocks.find(MapTriggerLogicBlock(distance)) ) 
														!= trigger.logicBlocks.end() )
			{
				XML_Element("LogicBlock");
				if ( logicIter->negated )
					XML_Attribute("not", "true");
				else
					XML_Attribute("not", "false");
			}
			
			{
				XML_Element("Condition");
				XML_Attribute("name", it2->name);

				XML_Attribute("function", it2->functionName);
				XML_Attribute("display", it2->displayName);

				if ( it2->negated )
					XML_Attribute("not", "true");
				else
					XML_Attribute("not", "false");
			
				for ( std::list<CStrW>::const_iterator paramIter = it2->parameters.begin(); 
											paramIter != it2->parameters.end(); ++paramIter )
				{
					CStrW paramString(*paramIter);
					//paramString.Replace(L"<", L"&lt;");
					//paramString.Replace(L">", L"&gt;");
					XML_Setting("Parameter", paramString);
				}
				if ( it2->linkLogic == 1 )
					XML_Setting("LinkLogic", "AND");
				else if ( it2->linkLogic == 2 )
					XML_Setting("LinkLogic", "OR");
					
				if ( trigger.logicBlockEnds.find(distance) != trigger.logicBlockEnds.end() )
				{
						XML_Element("LogicBlockEnd");
				}
			}
		}	//Read all conditions		
	}	//Conditions' scope
		
	{
		XML_Element("Effects");
		for ( std::list<MapTriggerEffect>::const_iterator it2 = trigger.effects.begin(); 
												it2 != trigger.effects.end(); ++it2 )
		{
			XML_Element("Effect");
			XML_Attribute("name", it2->name);
			{
				XML_Setting("function", it2->functionName);	
				XML_Setting("display", it2->displayName);

				for ( std::list<CStrW>::const_iterator paramIter = it2->parameters.begin();
											paramIter != it2->parameters.end(); ++paramIter )
				{
					XML_Setting("Parameter", *paramIter);
				}
			}
		}
	}	//Effects' scope	
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// RewriteAllMaps
void CMapWriter::RewriteAllMaps(CTerrain* pTerrain, CUnitManager* pUnitMan,
								WaterManager* pWaterMan, SkyManager* pSkyMan,
								CLightEnv* pLightEnv, CCamera* pCamera, CCinemaManager* pCinema,
								CTriggerManager* pTrigMan, CSimulation2* pSimulation2, CEntityManager* pEntityMan)
{
	VfsPaths pathnames;
	(void)fs_util::GetPathnames(g_VFS, L"maps/scenarios", L"*.pmp", pathnames);
	for (size_t i = 0; i < pathnames.size(); i++)
	{
		CMapReader* reader = new CMapReader;
		LDR_BeginRegistering();
		reader->LoadMap(pathnames[i], pTerrain, pUnitMan, pWaterMan, pSkyMan, pLightEnv, pCamera, pCinema, pTrigMan, pSimulation2, pEntityMan);
		LDR_EndRegistering();
		LDR_NonprogressiveLoad();

		CStrW newPathname(pathnames[i].string());
		newPathname.Replace(L"scenarios/", L"scenarios/new/");
		CMapWriter writer;
		writer.SaveMap(newPathname, pTerrain, pUnitMan, pWaterMan, pSkyMan, pLightEnv, pCamera, pCinema);
	}
}
