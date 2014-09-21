




// DO NOT EDIT !
// This file is generated using the MantaFlow preprocessor (prep generate).




/******************************************************************************
 *
 * MantaFlow fluid solver framework
 * Copyright 2011 Tobias Pfaff, Nils Thuerey 
 *
 * This program is free software, distributed under the terms of the
 * GNU General Public License (GPL) 
 * http://www.gnu.org/licenses
 *
 * Loading and writing grids and meshes to disk
 *
 ******************************************************************************/

#ifndef _FILEIO_H
#define _FILEIO_H

#include <string>

namespace Manta {

// forward decl.
class Mesh;
class FlagGrid;
template<class T> class Grid;
class BasicParticleSystem;
template<class T> class ParticleDataImpl;

void writeObjFile(const std::string& name, Mesh* mesh);
void writeBobjFile(const std::string& name, Mesh* mesh);
void readObjFile(const std::string& name, Mesh* mesh, bool append);
void readBobjFile(const std::string& name, Mesh* mesh, bool append);

template<class T> void writeGridRaw(const std::string& name, Grid<T>* grid);
template<class T> void writeGridUni(const std::string& name, Grid<T>* grid);
template<class T> void writeGridVol(const std::string& name, Grid<T>* grid);
template<class T> void writeGridTxt(const std::string& name, Grid<T>* grid);

template<class T> void readGridUni(const std::string& name, Grid<T>* grid);
template<class T> void readGridRaw(const std::string& name, Grid<T>* grid);

template <class T> void writeParticlesUni(const std::string& name, BasicParticleSystem* parts );
template <class T> void readParticlesUni (const std::string& name, BasicParticleSystem* parts );

template <class T> void writePdataUni(const std::string& name, ParticleDataImpl<T>* pdata );
template <class T> void readPdataUni (const std::string& name, ParticleDataImpl<T>* pdata );

} // namespace

#endif


