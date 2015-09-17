/*  An open source Navier-Stokes CFD solver.
    Copyright (C) 2015  Michael Nucci (michael.nucci@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <iostream>               // cout, cerr, endl
#include <algorithm>              // max, min
#include <vector>
#include <string>
#include "procBlock.hpp"
#include "plot3d.hpp"              // plot3d
#include "eos.hpp"                 // idealGas
#include "inviscidFlux.hpp"        // inviscidFlux
#include "viscousFlux.hpp"         // viscousFlux
#include "input.hpp"               // inputVars
#include "turbulence.hpp"
#include "gradients.hpp"
#include "slices.hpp"
#include "source.hpp"
#include "resid.hpp"

using std::cout;
using std::endl;
using std::cerr;
using std::vector;
using std::string;
using std::max;
using std::min;

// constructors for procBlock class
procBlock::procBlock() {
  numGhosts_ = 0;
  parBlock_ = 0;
  rank_ = 0;
  globalPos_ = 0;
  localPos_ = 0;

  // size vectors holding cell center and cell face values appropriately
  state_ = multiArray3d<primVars>(1, 1, 1);

  center_ = multiArray3d<vector3d<double>>(1, 1, 1);
  fAreaI_ = multiArray3d<unitVec3dMag<double>>(1, 1, 1);
  fAreaJ_ = multiArray3d<unitVec3dMag<double>>(1, 1, 1);
  fAreaK_ = multiArray3d<unitVec3dMag<double>>(1, 1, 1);
  fCenterI_ = multiArray3d<vector3d<double>>(1, 1, 1);
  fCenterJ_ = multiArray3d<vector3d<double>>(1, 1, 1);
  fCenterK_ = multiArray3d<vector3d<double>>(1, 1, 1);

  residual_ = multiArray3d<genArray>(1, 1, 1);

  vol_ = multiArray3d<double>(1, 1, 1);
  avgWaveSpeed_ = multiArray3d<double>(1, 1, 1);
  dt_ = multiArray3d<double>(1, 1, 1);
  wallDist_ = multiArray3d<double>(1, 1, 1);

  bc_ = boundaryConditions();
}

// constructor -- assign passed state to initialize state vector
procBlock::procBlock(const primVars &inputState, const plot3dBlock &blk,
                     const int &numBlk, const int &numG,
                     const boundaryConditions &bound, const int &pos,
                     const int &r, const int &lpos) {
  // inputState -- state_ to initialize block with (primative)
  // blk -- plot3d block of which this procBlock is a subset of
  // numBlk -- the block number of blk (the parent block)
  // numG -- number of ghost cells
  // bound -- boundary conditions for block
  // pos -- global position of block, an identifying number unique to this block
  // r -- processor rank_ that procBlock should be on
  // lpos -- local position of block on processor

  numGhosts_ = numG;
  parBlock_ = numBlk;

  rank_ = r;
  globalPos_ = pos;
  localPos_ = lpos;

  bc_ = bound;

  // pad stored variable vectors with ghost cells
  state_ = PadWithGhosts(multiArray3d<primVars>(blk.NumI() - 1, blk.NumJ() - 1,
                                                blk.NumK() - 1, inputState),
                         numGhosts_);

  vol_ = PadWithGhosts(blk.Volume(), numGhosts_);
  center_ = PadWithGhosts(blk.Centroid(), numGhosts_);
  fAreaI_ = PadWithGhosts(blk.FaceAreaI(), numGhosts_);
  fAreaJ_ = PadWithGhosts(blk.FaceAreaJ(), numGhosts_);
  fAreaK_ = PadWithGhosts(blk.FaceAreaK(), numGhosts_);
  fCenterI_ = PadWithGhosts(blk.FaceCenterI(), numGhosts_);
  fCenterJ_ = PadWithGhosts(blk.FaceCenterJ(), numGhosts_);
  fCenterK_ = PadWithGhosts(blk.FaceCenterK(), numGhosts_);

  avgWaveSpeed_ = multiArray3d<double>(blk.NumI() - 1, blk.NumJ() - 1,
                                       blk.NumK() - 1);
  dt_ = multiArray3d<double>(blk.NumI() - 1, blk.NumJ() - 1, blk.NumK() - 1);
  wallDist_ = multiArray3d<double>(blk.NumI() - 1, blk.NumJ() - 1,
                                   blk.NumK() - 1, DEFAULTWALLDIST);
  residual_ = multiArray3d<genArray>(blk.NumI() - 1, blk.NumJ() - 1,
                                     blk.NumK() - 1);
}

// constructor -- allocate space for procBlock
procBlock::procBlock(const int &ni, const int &nj, const int &nk,
                     const int &numG) {
  // ni -- i-dimension (cell)
  // nj -- j-dimension (cell)
  // nk -- k-dimension (cell)
  // numG -- number of ghost cell layers

  numGhosts_ = numG;
  parBlock_ = 0;

  rank_ = 0;
  globalPos_ = 0;
  localPos_ = 0;

  bc_ = boundaryConditions();

  // pad stored variable vectors with ghost cells
  state_ = multiArray3d<primVars>(ni + 2 * numG, nj + 2 * numG, nk + 2 * numG);
  center_ = multiArray3d<vector3d<double>>(ni + 2 * numG, nj + 2 * numG,
                                            nk + 2 * numG);
  fAreaI_ = multiArray3d<unitVec3dMag<double>>(ni + 2 * numG + 1,
                                                nj + 2 * numG, nk + 2 * numG);
  fAreaJ_ = multiArray3d<unitVec3dMag<double>>(ni + 2 * numG,
                                                nj + 2 * numG + 1,
                                                nk + 2 * numG);
  fAreaK_ = multiArray3d<unitVec3dMag<double>>(ni + 2 * numG,
                                                nj + 2 * numG,
                                                nk + 2 * numG + 1);
  fCenterI_ = multiArray3d<vector3d<double>>(ni + 2 * numG + 1, nj + 2 * numG,
                                              nk + 2 * numG);
  fCenterJ_ = multiArray3d<vector3d<double>>(ni + 2 * numG, nj + 2 * numG + 1,
                                              nk + 2 * numG);
  fCenterK_ = multiArray3d<vector3d<double>>(ni + 2 * numG, nj + 2 * numG,
                                              nk + 2 * numG + 1);
  residual_ = multiArray3d<genArray>(ni, nj, nk);
  vol_ = multiArray3d<double>(ni + 2 * numG, nj + 2 * numG, nk + 2 * numG);
  avgWaveSpeed_ = multiArray3d<double>(ni, nj, nk);
  dt_ = multiArray3d<double>(ni, nj, nk);
  wallDist_ = multiArray3d<double>(ni, nj, nk);
}

// member function to add a member of the inviscid flux class to the residual
void procBlock::AddToResidual(const inviscidFlux &flux, const int &ii,
                              const int &jj, const int &kk) {
  // flux -- inviscid flux to add to residual
  // ii -- i-location of residual to add to
  // jj -- j-location of residual to add to
  // kk -- k-location of residual to add to

  residual_(ii, jj, kk)[0] += flux.RhoVel();
  residual_(ii, jj, kk)[1] += flux.RhoVelU();
  residual_(ii, jj, kk)[2] += flux.RhoVelV();
  residual_(ii, jj, kk)[3] += flux.RhoVelW();
  residual_(ii, jj, kk)[4] += flux.RhoVelH();
  residual_(ii, jj, kk)[5] += flux.RhoVelK();
  residual_(ii, jj, kk)[6] += flux.RhoVelO();
}

// member function to add a member of the viscous flux class to the residual_
void procBlock::AddToResidual(const viscousFlux &flux, const int &ii,
                              const int &jj, const int &kk) {
  // flux -- inviscid flux to add to residual_
  // ii -- location of residual_ to add to
  // jj -- j-location of residual to add to
  // kk -- k-location of residual to add to

  residual_(ii, jj, kk)[1] += flux.MomX();
  residual_(ii, jj, kk)[2] += flux.MomY();
  residual_(ii, jj, kk)[3] += flux.MomZ();
  residual_(ii, jj, kk)[4] += flux.Engy();
  residual_(ii, jj, kk)[5] += flux.MomK();
  residual_(ii, jj, kk)[6] += flux.MomO();
}

// member function to add a member of the inviscid source class to the residual
void procBlock::AddToResidual(const source &src, const int &ii,
                              const int &jj, const int &kk) {
  // src -- source to add to residual
  // ii -- location of residual to add to
  // jj -- j-location of residual to add to
  // kk -- k-location of residual to add to

  residual_(ii, jj, kk)[0] += src.SrcMass();
  residual_(ii, jj, kk)[1] += src.SrcMomX();
  residual_(ii, jj, kk)[2] += src.SrcMomY();
  residual_(ii, jj, kk)[3] += src.SrcMomZ();
  residual_(ii, jj, kk)[4] += src.SrcEngy();
  residual_(ii, jj, kk)[5] += src.SrcTke();
  residual_(ii, jj, kk)[6] += src.SrcOmg();
}

//---------------------------------------------------------------------
// function declarations

/* Function to calculate the inviscid fluxes on the i-faces. All phyiscal
(non-ghost) i-faces are looped over. The left and right states are
calculated, and then the flux at the face is calculated. The flux at the
face contributes to the residual of the cells to the left and right of
the face. This contribution from the flux is added to the residuals and
the wave speed is accumulated as well.
  ___________________________
  |            |            |
  |            |            |
  |   Ui       -->   Ui+1   |
  |            |            |
  |____________|____________|
Ui-1/2       Ui+1/2       Ui+3/2

Using the above diagram, the flux is calculated at face Ui+1/2. Since the area
vector at the face always points from lower indices to higher indices it
points from Ui to Ui+1. For the residual calculation the convention is for the area
vector to point out of the cell. Therefore it is in the correct position for the
residual at Ui, but the sign needs to be flipped when adding the contribution of
the flux to the residual at Ui+1.

The spectral radius in the i-direction is also calculated. Since this is done on
a cell basis instead of a face bases, it is only calculated for the upper cell
(Ui+1 in this case). The spectral radius is added to the average wave speed
variable and is eventually used in the time step calculation if the time step
isn't explicitly specified.
*/
void procBlock::CalcInvFluxI(const idealGas &eqnState, const input &inp) {
  // eqnState -- equation of state
  // inp -- all input variables


  // loop over all physical i-faces
  // in struct p is for physical index, g is for index with ghosts
  for (struct {int p; int g;} kk = {0, numGhosts_}; kk.g <
           fAreaI_.NumK() - numGhosts_; kk.g++, kk.p++) {
    for (struct {int p; int g;} jj = {0, numGhosts_}; jj.g <
             fAreaI_.NumJ() - numGhosts_; jj.g++, jj.p++) {
      for (struct {int p; int g;} ii = {0, numGhosts_}; ii.g <
               fAreaI_.NumI() - numGhosts_; ii.g++, ii.p++) {
        primVars faceStateLower, faceStateUpper;

        // use constant reconstruction (first order)
        if (inp.OrderOfAccuracy() == "first") {
          faceStateLower = state_(ii.g - 1, jj.g, kk.g).FaceReconConst();
          faceStateUpper = state_(ii.g, jj.g, kk.g).FaceReconConst();
        } else {  // second order accuracy -- use MUSCL extrapolation
          // length of second upwind, first upwind, and downwind cells in
          // i-direction
          double upwind2L = fCenterI_(ii.g - 1, jj.g, kk.g).
              Distance(fCenterI_(ii.g - 2, jj.g, kk.g));
          double upwindL = fCenterI_(ii.g, jj.g, kk.g).
              Distance(fCenterI_(ii.g - 1, jj.g, kk.g));
          double downwindL = fCenterI_(ii.g, jj.g, kk.g).
              Distance(fCenterI_(ii.g + 1, jj.g, kk.g));

          faceStateLower = state_(ii.g - 1, jj.g, kk.g).FaceReconMUSCL(
              state_(ii.g - 2, jj.g, kk.g), state_(ii.g, jj.g, kk.g),
              inp.Kappa(), inp.Limiter(), upwindL, upwind2L, downwindL);

          // length of second upwind, first upwind, and downwind cells in
          // i-direction
          double upwind2U = fCenterI_(ii.g + 1, jj.g, kk.g).
              Distance(fCenterI_(ii.g + 2, jj.g, kk.g));
          double upwindU = fCenterI_(ii.g, jj.g, kk.g).
              Distance(fCenterI_(ii.g + 1, jj.g, kk.g));
          double downwindU = fCenterI_(ii.g, jj.g, kk.g).
              Distance(fCenterI_(ii.g - 1, jj.g, kk.g));

          faceStateUpper = state_(ii.g, jj.g, kk.g).FaceReconMUSCL(
              state_(ii.g + 1, jj.g, kk.g), state_(ii.g - 1, jj.g, kk.g),
              inp.Kappa(), inp.Limiter(), upwindU, upwind2U, downwindU);
        }
        // calculate Roe flux at face
        inviscidFlux tempFlux = RoeFlux(faceStateLower, faceStateUpper,
                                        eqnState,
                                        this->FAreaUnitI(ii.g, jj.g, kk.g));

        // area vector points from left to right, so add to left cell, subtract
        // from right cell
        // at left boundary there is no left cell to add to
        if (ii.g > numGhosts_) {
          this->AddToResidual(tempFlux * this->FAreaMagI(ii.g, jj.g, kk.g),
                              ii.p - 1, jj.p, kk.p);
        }
        // at right boundary there is no right cell to add to
        if (ii.g < fAreaI_.NumI() - numGhosts_ - 1) {
          this->AddToResidual(-1.0 * tempFlux *
                              this->FAreaMagI(ii.g, jj.g, kk.g),
                              ii.p, jj.p, kk.p);
          // calculate component of wave speed. This is done on a cell by cell
          // basis, so only at the upper faces
          avgWaveSpeed_(ii.p, jj.p, kk.p) +=
              CellSpectralRadius(fAreaI_(ii.g, jj.g, kk.g),
                                 fAreaI_(ii.g + 1, jj.g, kk.g),
                                 state_(ii.g, jj.g, kk.g), eqnState);
        }
      }
    }
  }
}

/* Function to calculate the inviscid fluxes on the j-faces. All physical
(non-ghost) j-faces are looped over. The left and right states are
calculated, and then the flux at the face is calculated. The flux at the
face contributes to the residual of the cells to the left and right of the
face. This contribution from the flux is added to the residuals and the wave
speed is accumulated as well.
  ___________________________
  |            |            |
  |            |            |
  |   Uj       -->   Uj+1   |
  |            |            |
  |____________|____________|
Uj-1/2       Uj+1/2       Uj+3/2

Using the above diagram, the flux is calculated at face Uj+1/2. Since the area
vector at the face always points from lower indices to higher indices it points
from Uj to Uj+1. For the residual calculation the convention is for the area
vector to point out of the cell. Therefore it is in the correct position for the
residual at Uj, but the sign needs to be flipped when adding the contribution of
the flux to the residual at Uj+1.

The spectral radius in the j-direction is also calculated. Since this is done on
a cell basis instead of a face bases, it is only calculated for the upper cell
(Uj+1 in this case). The spectral radius is added to the average wave speed
variable and is eventually used in the time step calculation if the time step
isn't explicitly specified.
*/
void procBlock::CalcInvFluxJ(const idealGas &eqnState, const input &inp) {
  // eqnState -- equation of state
  // inp -- all input variables

  // loop over all physical j-faces
  // in struct p is for physical index, g is for index with ghosts
  for (struct {int p; int g;} kk = {0, numGhosts_}; kk.g <
           fAreaJ_.NumK() - numGhosts_; kk.g++, kk.p++) {
    for (struct {int p; int g;} jj = {0, numGhosts_}; jj.g <
             fAreaJ_.NumJ() - numGhosts_; jj.g++, jj.p++) {
      for (struct {int p; int g;} ii = {0, numGhosts_}; ii.g <
               fAreaJ_.NumI() - numGhosts_; ii.g++, ii.p++) {
        primVars faceStateLower, faceStateUpper;

        // use constant reconstruction (first order)
        if (inp.OrderOfAccuracy() == "first") {
          faceStateLower = state_(ii.g, jj.g - 1, kk.g).FaceReconConst();
          faceStateUpper = state_(ii.g, jj.g, kk.g).FaceReconConst();
        } else {  // second order accuracy -- use MUSCL extrapolation
          // length of second upwind, first upwind, and downwind cells in
          // j-direction
          double upwind2L = fCenterJ_(ii.g, jj.g - 1, kk.g).
              Distance(fCenterJ_(ii.g, jj.g - 2, kk.g));
          double upwindL = fCenterJ_(ii.g, jj.g, kk.g).
              Distance(fCenterJ_(ii.g, jj.g - 1, kk.g));
          double downwindL = fCenterJ_(ii.g, jj.g, kk.g).
              Distance(fCenterJ_(ii.g, jj.g + 1, kk.g));

          faceStateLower = state_(ii.g, jj.g - 1, kk.g).FaceReconMUSCL(
              state_(ii.g, jj.g - 2, kk.g), state_(ii.g, jj.g, kk.g),
              inp.Kappa(), inp.Limiter(), upwindL, upwind2L, downwindL);

          // length of second upwind, first upwind, and downwind cells in
          // j-direction
          double upwind2U = fCenterJ_(ii.g, jj.g + 1, kk.g).
              Distance(fCenterJ_(ii.g, jj.g + 2, kk.g));
          double upwindU = fCenterJ_(ii.g, jj.g, kk.g).
              Distance(fCenterJ_(ii.g, jj.g + 1, kk.g));
          double downwindU = fCenterJ_(ii.g, jj.g, kk.g).
              Distance(fCenterJ_(ii.g, jj.g - 1, kk.g));

          faceStateUpper = state_(ii.g, jj.g, kk.g).FaceReconMUSCL(
              state_(ii.g, jj.g + 1, kk.g), state_(ii.g, jj.g - 1, kk.g),
              inp.Kappa(), inp.Limiter(), upwindU, upwind2U, downwindU);
        }

        // calculate Roe flux at face
        inviscidFlux tempFlux = RoeFlux(faceStateLower, faceStateUpper,
                                        eqnState,
                                        this->FAreaUnitJ(ii.g, jj.g, kk.g));

        // area vector points from left to right, so add to left cell, subtract
        // from right cell
        // at left boundary no left cell to add to
        if (jj.g > numGhosts_) {
          this->AddToResidual(tempFlux * this->FAreaMagJ(ii.g, jj.g, kk.g),
                              ii.p, jj.p - 1, kk.p);
        }
        // at right boundary no right cell to add to
        if (jj.g < fAreaJ_.NumJ() - numGhosts_ - 1) {
          this->AddToResidual(-1.0 * tempFlux *
                              this->FAreaMagJ(ii.g, jj.g, kk.g),
                              ii.p, jj.p, kk.p);

          // calculate component of wave speed. This is done on a cell by cell
          // basis, so only at the upper faces
          avgWaveSpeed_(ii.p, jj.p, kk.p) +=
              CellSpectralRadius(fAreaJ_(ii.g, jj.g, kk.g),
                                 fAreaJ_(ii.g, jj.g + 1, kk.g),
                                 state_(ii.g, jj.g, kk.g), eqnState);
        }
      }
    }
  }
}

/* Function to calculate the inviscid fluxes on the k-faces. All phyiscal
(non-ghost) k-faces are looped over. The left and right states are calculated,
and then the flux at the face is calculated. The flux at the face contributes
to the residual of the cells to the left and right of the face. This
contribution from the flux is added to the residuals and the wave speed is
accumulated as well.
  ___________________________
  |            |            |
  |            |            |
  |   Uk       -->   Uk+1   |
  |            |            |
  |____________|____________|
Uk-1/2       Uk+1/2       Uk+3/2

Using the above diagram, the flux is calculated at face Uk+1/2. Since the area
vector at the face always points from lower indices to higher indices it points
from Uk to Uk+1. For the residual calculation the convention is for the area
vector to point out of the cell. Therefore it is in the correct position for the
residual at Uk, but the sign needs to be flipped when adding the contribution of
the flux to the residual at Uk+1.

The spectral radius in the k-direction is also calculated. Since this is done on
a cell basis instead of a face bases, it is only calculated for the upper cell
(Uk+1 in this case). The spectral radius is added to the average wave speed
variable and is eventually used in the time step calculation if the time step
isn't explicitly specified.
*/
void procBlock::CalcInvFluxK(const idealGas &eqnState, const input &inp) {
  // eqnState -- equation of state
  // inp -- all input variables

  // loop over all physical k-faces
  // in struct p is for physical index, g is for index with ghosts
  for (struct {int p; int g;} kk = {0, numGhosts_}; kk.g <
           fAreaK_.NumK() - numGhosts_; kk.g++, kk.p++) {
    for (struct {int p; int g;} jj = {0, numGhosts_}; jj.g <
             fAreaK_.NumJ() - numGhosts_; jj.g++, jj.p++) {
      for (struct {int p; int g;} ii = {0, numGhosts_}; ii.g <
               fAreaK_.NumI() - numGhosts_; ii.g++, ii.p++) {
        primVars faceStateLower, faceStateUpper;

        // use constant reconstruction (first order)
        if (inp.OrderOfAccuracy() == "first") {
          faceStateLower = state_(ii.g, jj.g, kk.g - 1).FaceReconConst();
          faceStateUpper = state_(ii.g, jj.g, kk.g).FaceReconConst();
        } else {  // second order accuracy -- use MUSCL extrapolation
          // length of second upwind, first upwind, and downwind cells in
          // j-direction
          double upwind2L = fCenterK_(ii.g, jj.g, kk.g - 1).
              Distance(fCenterK_(ii.g, jj.g, kk.g - 2));
          double upwindL = fCenterK_(ii.g, jj.g, kk.g).
              Distance(fCenterK_(ii.g, jj.g, kk.g - 1));
          double downwindL = fCenterK_(ii.g, jj.g, kk.g).
              Distance(fCenterK_(ii.g, jj.g, kk.g + 1));

          faceStateLower = state_(ii.g, jj.g, kk.g - 1).FaceReconMUSCL(
              state_(ii.g, jj.g, kk.g - 2), state_(ii.g, jj.g, kk.g),
              inp.Kappa(), inp.Limiter(), upwindL, upwind2L, downwindL);

          // length of second upwind, first upwind, and downwind cells in
          // j-direction
          double upwind2U = fCenterK_(ii.g, jj.g, kk.g + 1).
              Distance(fCenterK_(ii.g, jj.g, kk.g + 2));
          double upwindU = fCenterK_(ii.g, jj.g, kk.g).
              Distance(fCenterK_(ii.g, jj.g, kk.g + 1));
          double downwindU = fCenterK_(ii.g, jj.g, kk.g).
              Distance(fCenterK_(ii.g, jj.g, kk.g - 1));

          faceStateUpper = state_(ii.g, jj.g, kk.g).FaceReconMUSCL(
              state_(ii.g, jj.g, kk.g + 1), state_(ii.g, jj.g, kk.g - 1),
              inp.Kappa(), inp.Limiter(), upwindU, upwind2U, downwindU);
        }

        // calculate Roe flux at face
        inviscidFlux tempFlux = RoeFlux(faceStateLower, faceStateUpper,
                                        eqnState,
                                        this->FAreaUnitK(ii.g, jj.g, kk.g));

        // area vector points from left to right, so add to left cell, subtract
        // from right cell
        // at left boundary no left cell to add to
        if (kk.g > numGhosts_) {
          this->AddToResidual(tempFlux *
                              this->FAreaMagK(ii.g, jj.g, kk.g),
                              ii.p, jj.p, kk.p - 1);
        }
        // at right boundary no right cell to add to
        if (kk.g < fAreaK_.NumK() - numGhosts_ - 1) {
          this->AddToResidual(-1.0 * tempFlux *
                              this->FAreaMagK(ii.g, jj.g, kk.g),
                              ii.p, jj.p, kk.p);

          // calculate component of wave speed. This is done on a cell by cell
          // basis, so only at the upper faces
          avgWaveSpeed_(ii.p, jj.p, kk.p) +=
              CellSpectralRadius(fAreaK_(ii.g, jj.g, kk.g),
                                 fAreaK_(ii.g, jj.g, kk.g + 1),
                                 state_(ii.g, jj.g, kk.g), eqnState);
        }
      }
    }
  }
}

/* Member function to calculate the local time step. (i,j,k) are cell indices.
The following equation is used:

dt = CFL * V / (Lci + Lcj + Lck + C * (Lvi + Lvj + Lvk)) (Blazek 6.18)

In the above equation dt is the time step, CFL is the CFL number, V is the cell
volume, Lci, Lcj, Lck are the convective (inviscid) spectral radii in the i, j,
and k directions, C is a constant (typical value b/w 1 and 4), and Lvi, Lvj,
Lvk are the viscous spectral radii. This function is only used when the time
step isn't explicitly defined by the user.
*/
void procBlock::CalcCellDt(const int &ii, const int &jj, const int &kk,
                           const double &cfl) {
  // ii -- i index of cell
  // jj -- j index of cell
  // kk -- k index of cell
  // cfl -- cfl number

  // use nondimensional time
  dt_(ii, jj, kk) = cfl * (vol_(ii + numGhosts_, jj + numGhosts_,
                                kk + numGhosts_) /
                           avgWaveSpeed_(ii, jj, kk));
}

/* Member function to calculate the time step for all cells in the procBlock. If
the time step is user specified assign that time step (after
nondimensionalization) to dt variable. If time step is to be determined using CFL
number, call function to do so.
*/
void procBlock::CalcBlockTimeStep(const input &inputVars, const double &aRef) {
  // inputVars -- all input variables
  // aRef -- reference speed of sound (used for time non dimensionalization)

  // loop over all physical cells - no ghost cells for dt variable
  for (int kk = 0; kk < this->NumK(); kk++) {
    for (int jj = 0; jj < this->NumJ(); jj++) {
      for (int ii = 0; ii < this->NumI(); ii++) {
        // dt specified, use global time stepping
        if (inputVars.Dt() > 0.0) {
          // nondimensional time
          dt_(ii, jj, kk) = inputVars.Dt() * aRef / inputVars.LRef();

        // cfl specified, use local time stepping
        } else if (inputVars.CFL() > 0.0) {
          this->CalcCellDt(ii, jj, kk, inputVars.CFL());
        } else {
          cerr << "ERROR: Neither dt or cfl was specified!" << endl;
          exit(0);
        }
      }
    }
  }
}

/* Member function to update the procBlock to advance to a new time step. For
explicit methods it calls the appropriate explicit method to update. For
implicit methods it uses the correction du and calls the implicit updater.
*/
void procBlock::UpdateBlock(const input &inputVars, const int &impFlag,
                            const idealGas &eos, const double &aRef,
                            const multiArray3d<genArray> &du, genArray &l2,
                            resid &linf) {
  // inputVars -- all input variables
  // impFlag -- flag to determine if simulation is to be solved via explicit or
  // implicit time stepping
  // eos -- equation of state
  // aRef -- reference speed of sound (for nondimensionalization)
  // bb
  // du -- updates to conservative variables (only used in implicit solver)
  // l2 -- l-2 norm of residual
  // linf -- l-infinity norm of residual

  // if not runge-kutta 4 step method for time integration
  if (inputVars.TimeIntegration() != "rk4") {
    // loop over all physical cells
    for (struct {int p; int g;} kk = {0, numGhosts_}; kk.p <
             this->NumK(); kk.g++, kk.p++) {
      for (struct {int p; int g;} jj = {0, numGhosts_}; jj.p <
               this->NumJ(); jj.g++, jj.p++) {
        for (struct {int p; int g;} ii = {0, numGhosts_}; ii.p <
                 this->NumI(); ii.g++, ii.p++) {
          // explicit euler time integration
          if (inputVars.TimeIntegration() == "explicitEuler") {
            this->ExplicitEulerTimeAdvance(eos, ii.g, jj.g, kk.g,
                                           ii.p, jj.p, kk.p);
          } else if (impFlag) {  // if implicit use update (du)
            this->ImplicitTimeAdvance(du(ii.p, jj.p, kk.p), eos,
                                      ii.g, jj.g, kk.g);
          }

          // accumulate l2 norm of residual
          l2 = l2 + residual_(ii.p, jj.p, kk.p) * residual_(ii.p, jj.p, kk.p);

          // if any residual is larger than previous residual, a new linf
          // residual is found
          for (int ll = 0; ll < NUMVARS; ll++) {
            if (this->Residual(ii.p, jj.p, kk.p, ll) > linf.Linf()) {
              linf.UpdateMax(this->Residual(ii.p, jj.p, kk.p, ll),
                             parBlock_, ii.p, jj.p, kk.p, ll + 1);
            }
          }
        }
      }
    }
  // using min storage rk4 method
  } else if (inputVars.TimeIntegration() == "rk4") {
    // save state and local time step at time n
    multiArray3d<primVars> stateN = state_;
    multiArray3d<double> dtN = dt_;

    // loop over rk stages
    for (int rr = 0; rr < 4; rr++) {
      // loop over all physical cells
      for (struct {int p; int g;} kk = {0, numGhosts_}; kk.p <
               this->NumK(); kk.g++, kk.p++) {
        for (struct {int p; int g;} jj = {0, numGhosts_}; jj.p <
                 this->NumJ(); jj.g++, jj.p++) {
          for (struct {int p; int g;} ii = {0, numGhosts_}; ii.p <
                   this->NumI(); ii.g++, ii.p++) {
            // advance 1 RK stage
            this->RK4TimeAdvance(stateN(ii.g, jj.g, kk.g), eos,
                                 ii.g, jj.g, kk.g,
                                 ii.p, jj.p, kk.p, rr);

            // at last stage
            // accumulate l2 norm of residual
            if (rr == 3) {
              l2 = l2 + residual_(ii.p, jj.p, kk.p) *
                  residual_(ii.p, jj.p, kk.p);

              for (int ll = 0; ll < NUMVARS; ll++) {
                if (this->Residual(ii.p, jj.p, kk.p, ll) > linf.Linf()) {
                  linf.UpdateMax(this->Residual(ii.p, jj.p, kk.p, ll),
                                 parBlock_, ii.p, jj.p, kk.p, ll + 1);
                }
              }
            }
          }
        }
      }
      // for multistage RK4 method, calculate fluxes and residuals again
      if (rr < 3) {  // no need to calculate fluxes after final RK interation
        // UPDATE NEEDED -- have to calculate grads, visc fluxes, source terms
        // again. Maybe BCs as well
        this->CalcInvFluxI(eos, inputVars);
        this->CalcInvFluxJ(eos, inputVars);
        this->CalcInvFluxK(eos, inputVars);
        this->CalcBlockTimeStep(inputVars, aRef);
      }
    }
  } else {
    cerr << "ERROR: Time integration scheme " << inputVars.TimeIntegration()
         << " is not recognized!" << endl;
  }
}

/* Member function to advance the state_ vector to time n+1 using explicit Euler
method. The following equation is used:

 Un+1 = Un - dt_/V * R

Un is the conserved variables at time n, Un+1 is the conserved variables at time
n+1, dt_ is the cell's time step, V is the cell's volume, and R is the cell's
residual.
 */
void procBlock::ExplicitEulerTimeAdvance(const idealGas &eqnState,
                                         const int &ig, const int &jg,
                                         const int &kg, const int &ip,
                                         const int &jp, const int &kp) {
  // eqnState -- equation of state
  // ig -- i-location of cell (including ghost cells)
  // jg -- j-location of cell (including ghost cells)
  // kg -- k-location of cell (including ghost cells)
  // ip -- i-location of cell (without ghost cells)
  // jp -- j-location of cell (without ghost cells)
  // kp -- k-location of cell (without ghost cells)

  // Get conserved variables for current state (time n)
  genArray consVars = state_(ig, jg, kg).ConsVars(eqnState);
  // calculate updated conserved variables
  consVars = consVars - dt_(ip, jp, kp) / vol_(ig, jg, kg) *
      residual_(ip, jp, kp);

  // calculate updated primative variables and update state
  state_(ig, jg, kg) = primVars(consVars, false, eqnState);
}

// member function to advance the state_ vector to time n+1 (for implicit
// methods)
void procBlock::ImplicitTimeAdvance(const genArray &du,
                                    const idealGas &eqnState, const int &ii,
                                    const int &jj, const int &kk) {
  // du -- update for a specific cell (to move from time n to n+1)
  // eqnState -- equation of state
  // ii -- i-location of cell (with ghosts)
  // jj -- j-location of cell (with ghosts)
  // kk -- k-location of cell (with ghosts)

  // calculate updated state (primative variables)
  state_(ii, jj, kk) = state_(ii, jj, kk).UpdateWithConsVars(eqnState, du);
}

/*member function to advance the state_ vector to time n+1 using 4th order
(minimum storage) Runge-Kutta method (2nd order accurate)

 Un+1 = Un - dt/V * alpha * R

Un is the conserved variables at time n, Un+1 is the conserved variables at time
n+1, dt_ is the cell's time step, V is the cell's volume, alpha is the runge-kutta
coefficient, and R is the cell's residual.
 */
void procBlock::RK4TimeAdvance(const primVars &currState,
                               const idealGas &eqnState, const int &ig,
                               const int &jg, const int &kg, const int &ip,
                               const int &jp, const int &kp, const int &rk) {
  // currState -- current state_ (including steps within RK4) (primative)
  // eqnState -- equation of state
  // ig -- i-location of cell (including ghost cells)
  // jg -- j-location of cell (including ghost cells)
  // kg -- k-location of cell (including ghost cells)
  // ip -- i-location of cell (without ghost cells)
  // jp -- j-location of cell (without ghost cells)
  // kp -- k-location of cell (without ghost cells)
  // rk -- runge-kutta step number

  // runge-kutta step coefficients (low storage 4 step)
  double alpha[4] = {0.25, 1.0 / 3.0, 0.5, 1.0};

  // update conserved variables
  genArray consVars = currState.ConsVars(eqnState) -
      dt_(ip, jp, kp) / vol_(ig, jg, kg) * alpha[rk] * residual_(ip, jp, kp);

  // calculate updated primative variables
  state_(ig, jg, kg) = primVars(consVars, false, eqnState);
}

// member function to reset the residual and wave speed back to zero after an
// iteration. This is done because the residual and wave
// speed are accumulated over many function calls.
void procBlock::ResetResidWS() {
  // loop over all physical cells - no ghost cells in residual variable
  for (int kk = 0; kk < this->NumK(); kk++) {
    for (int jj = 0; jj < this->NumJ(); jj++) {
      for (int ii = 0; ii < this->NumI(); ii++) {
        // reset residual
        residual_(ii, jj, kk) = genArray(0.0);

        // reset wave speed
        avgWaveSpeed_(ii, jj, kk) = 0.0;
      }
    }
  }
}

/* Member function to add the cell volume divided by the cell time step to the
main diagonal of the time m minus time n term.

dU/dt = V/t * [ ((1 + zeta) * FD - zeta * BD) / ((1 + theta) * FD )] * Un = -Rn

The above equation shows the governing equations written in the Beam & Warming
format for time integration. U is the vector of conserved variables
where n represents the time step. Theta and zeta are Beam & Warming parameters,
t is the time step, V is the cell volume, and R is the residual.
FD and BD are the forward and backward difference operators respectively. These
opererators operate in the time domain. For example FD(U) =
Un+1 - Un and BD(U) = Un - Un-1. Solving the above equation for FD(Qn) we get
the following:

FD(Un) = (-t * Rn - t * theta * FD(Rn) + zeta * V * FD(Un-1)) / ((1 + zeta) * V)

FD(Rn) requires us to know the residual at time n+1, but this is unknown. To
bypass this difficulty we linearize the residual_ using a Taylor series
expansion about time n. Rn+1 = Rn + J*FD(Un) where J is the flux jacobian dR/dU.
Rearranging the above equation we get the following:

[J + (1+zeta)*V/(t*theta)] * FD(Un) = -Rn/theta + zeta*V/(t*theta) * FD(Un-1)

The above equation shows that the time m minus time n term (FD(Un)) requires a
(1+zeta)V/(t*theta) term multiplied by it. That is the purpose of this
function.
*/
multiArray3d<genArray> procBlock::AddVolTime(const multiArray3d<genArray> &m,
                                             const multiArray3d<genArray> &n,
                                             const double &theta,
                                             const double &zeta) const {
  // m -- solution for block at time m
  // n -- solution for block at time n
  // theta -- Beam & Warming coefficient theta for time integration
  // zeta -- Beam & Warming coefficient zeta for time integration

  // initialize a vector to hold the returned values
  multiArray3d<genArray> mMinusN(m.NumI(), m.NumJ(), m.NumK());

  // loop over all physical cells
  for (struct {int p; int g;} kk = {0, numGhosts_}; kk.p <
           this->NumK(); kk.g++, kk.p++) {
    for (struct {int p; int g;} jj = {0, numGhosts_}; jj.p <
             this->NumJ(); jj.g++, jj.p++) {
      for (struct {int p; int g;} ii = {0, numGhosts_}; ii.p <
               this->NumI(); ii.g++, ii.p++) {
        double I = (vol_(ii.g, jj.g, kk.g) * (1.0 + zeta)) /
            (dt_(ii.p, jj.p, kk.p) * theta);
        mMinusN(ii.p, jj.p, kk.p) =
            I * (m(ii.p, jj.p, kk.p) - n(ii.p, jj.p, kk.p));
      }
    }
  }
  return mMinusN;
}

/* Member function to calculate the delta n-1 term for the implicit bdf2 solver.

dU/dt_ = V/t * [ ((1 + zeta) * FD - zeta * BD) / ((1 + theta) * FD )] * Un = -Rn

The above equation shows the governing equations written in the Beam & Warming
format for time integration. U is the vector of conserved variables
where n represents the time step. Theta and zeta are Beam & Warming parameters,
t is the time step, V is the cell volume, and R is the residual.
FD and BD are the forward and backward difference operators respectively. These
opererators operate in the time domain. For example FD(U) =
Un+1 - Un and BD(U) = Un - Un-1. Solving the above equation for FD(Qn) we get
the following:

FD(Un) = (-t * Rn - t * theta * FD(Rn) + zeta * V * FD(Un-1)) / ((1 + zeta) * V)

FD(Rn) requires us to know the residual at time n+1, but this is unknown. To
bypass this difficulty we linearize the residual using a Taylor series
expansion about time n. Rn+1 = Rn + J*FD(Un) where J is the flux jacobian dR/dU.
Rearranging the above equation we get the following:

[J + (1+zeta)*V/(t*theta)] * FD(Un) = -Rn/theta + zeta*V/(t*theta) * FD(Un-1)

The above equation shows that the time n minus time n-1 term (FD(Un-1)) requires
a zeta*V/(t*theta) term multiplied by it. That is the purpose of this
function.
*/
void procBlock::DeltaNMinusOne(multiArray3d<genArray> &solDeltaNm1,
                               const multiArray3d<genArray> &solTimeN,
                               const idealGas &eqnState, const double &theta,
                               const double &zeta) {
  // solDeltaNm1 -- The solution at time n minus the solution at time n-1. (Un -
  // Un-1) (output)
  // solTimeN -- The solution at time n
  // eqnState -- equation of state_
  // theta -- Beam & Warming coefficient theta for time integration
  // zeta -- Beam & Warming coefficient zeta for time integration

  // loop over physical cells
  for (struct {int p; int g;} kk = {0, numGhosts_}; kk.p <
           this->NumK(); kk.g++, kk.p++) {
    for (struct {int p; int g;} jj = {0, numGhosts_}; jj.p <
             this->NumJ(); jj.g++, jj.p++) {
      for (struct {int p; int g;} ii = {0, numGhosts_}; ii.p <
               this->NumI(); ii.g++, ii.p++) {
        double coeff = (vol_(ii.g, jj.g, kk.g) * zeta) /
            (dt_(ii.p, jj.p, kk.p) * theta);
        solDeltaNm1(ii.p, jj.p, kk.p) = coeff *
            (state_(ii.g, jj.g, kk.g).ConsVars(eqnState) -
             solTimeN(ii.p, jj.p, kk.p));
      }
    }
  }
}

// Member function to return a copy of the conserved variables. This is useful
// for "saving" the solution at a specific time.
// It is used in the implicit solver.
multiArray3d<genArray> procBlock::GetCopyConsVars(
    const idealGas &eqnState) const {
  // initialize array to proper size (no ghost cells)
  multiArray3d<genArray> consVars(this->NumI(), this->NumJ(),
                                  this->NumK());

  // loop over physical cells
  for (struct {int p; int g;} kk = {0, numGhosts_}; kk.p <
           consVars.NumK(); kk.g++, kk.p++) {
    for (struct {int p; int g;} jj = {0, numGhosts_}; jj.p <
             consVars.NumJ(); jj.g++, jj.p++) {
      for (struct {int p; int g;} ii = {0, numGhosts_}; ii.p <
               consVars.NumI(); ii.g++, ii.p++) {
        // convert state to conservative variables
        consVars(ii.p, jj.p, kk.p) =
            state_(ii.g, jj.g, kk.g).ConsVars(eqnState);
      }
    }
  }
  return consVars;
}

/* Member function to calculate update to solution implicitly using Lower-Upper
Symmetric Gauss Seidel (LUSGS) method.

Un+1 = Un - t/V * Rn+1

The above equation shows a simple first order implicit method to calculate the
solution at the next time step (n+1). The equation shows that this method
requires the residual (R) at time n+1 which is unknown. In the equation, t is
the time step, and V is the volume. Since the residual at n+1 in unknown, it
must be linearized about time n as shown below.

Rn+1 = Rn + dRn/dUn * FD(Un)

In the above equation FD is the forward difference operator in time (FD(Un) =
Un+1 - Un). The derivative of the residual term can be further simplified as
below.

Rn = (sum over all faces) Fni

In the above equation n refers to the time level and i refers to the face index.
The summation over all faces operator will now be abbreviated as (SF).
Substituting the second and third equations into the first and rearranging we
get the following.

[d(SF)Fni/dUnj + V/t] * FD(Un) = -Rn

In the above equation the index j refers to all of the cells in the stencil
going into the calculation of the flux at face i. The above equation
can be simplified to A*x=b. The matrix A is an MxM block matrix where M is the
number of cells in the block. Each block is an LxL block where L
is the number of equations being solved for. The sparsity of A depends on the
stencil used in flux calculation. In 3D for a first order simulation
the matrix A is block pentadiagonal. For a second order approximation it would
have 13 diagonals. This increases the storage requirements so in
practice a first order approximation is used. The order of accuracy is
determined by the residual calculation. The accuracy of the matrix A helps
with convergence. A poorer approximation will eventually get the correct answer
with enough iteration (defect correction). Fully implicit methods
calculate and store the flux jacobians needed to populate the matrix A. The
LUSGS method does not do this and instead calculates an approximate
flux jacobian "on-the-fly" so there is no need for storage. Because an
approximate flux jacobian is being used, there is no need for a highly
accurate linear solver. Therefore the Symmetric Gauss-Seidel (SGS) method is a
good candidate, and only one iteration is needed. This approximate
flux jacobian along with the SGS linear solver form the basics of the LUSGS
method (Jameson & Yoon). The LUSGS method begins by factoring the
matrix A as shown below.

A = (D + L) * D^-1 * (D + U)

In the above equation D is the diagonal of A, L is the lower triangular portion
of A, and U is the upper triangular portion of A. This allows the equation A*x=b
to be solved in one SGS sweep as shown below.

Forward sweep:  (D + L) * FD(Un*) = -Rn
Backward sweep: (D + U) * FD(Un) = D * FD(Un*)

Another key component of the LUSGS scheme is to sweep along hyperplanes.
Hyperplanes are planes of i+j+k=constant within a plot3d block. The diagram
below shows a 2D example of a block reordered to sweep along hyperplanes
           ____ ____ ____ ____ ____ ____ ____ ____
          | 20 | 26 | 32 | 37 | 41 | 44 | 46 | 47 |
          |____|____|____|____|____|____|____|____|
          | 14 | 19 | 25 | 31 | 36 | 40 | 43 | 45 |
          |____|____|____|____|____|____|____|____|
          | 9  | 13 | 18 | 24 | 30 | 35 | 39 | 42 |
   A=     |____|____|____|____|____|____|____|____|
          | 5  | 8  | 12 | 17 | 23 | 29 | 34 | 38 |
          |____|____|____|____|____|____|____|____|
          | 2  | 4  | 7  | 11 | 16 | 22 | 28 | 33 |
          |____|____|____|____|____|____|____|____|
          | 0  | 1  | 3  | 6  | 10 | 15 | 21 | 27 |
          |____|____|____|____|____|____|____|____|

This is advantageous because on the forward sweep the lower matrix L can be
calculated with data from time n+1 because all of the cells contributing
to L would already have been updated. The same is true with the upper matrix U
for the backward sweep. This removes the need for any storage of the
matrix. For a given location (say A12) the matrix L would be constructed of A8
and A7, and the matrix U would be constructed of A18 and A17. This
requires the product of the flux jacobian multiplied with the update (FD(Un)) to
be calculated at these locations. For the LUSGS method the flux
jacobians are approximated as follows:

A * S = 0.5 * (Ac * S + K * I)

A is the flux jacobian, S is the face area, Ac is the convective flux jacobian
(dF/dU), K is the spectral radius multiplied by a factor, and I is
the identity matrix. The addition of the spectrial radius improves diagonal
dominance which improves stability at the cost of convergence. When the
factor multiplied by K is 1, the method is SGS, when it is < 1, it is successive
overrelaxation. Reducing the factor improves convergence but hurts
stability. The product of the approximate flux jacobian with the update is
calculated as shown below.

A * S * FD(Unj) = 0.5 * (Ac * FD(Unj) * S + K * I * FD(Unj)) = 0.5 * ( dFi/dUnj
* FD(Unj) * S + K * I * FD(Unj)) = 0.5 * (dFi * S + K * I * FD(Unj))

The above equation shows that all that is needed to calculate the RHS of A*x=b
is the update of the convective flux, and the update to the convervative
variabes (FD(Unj)) which is known due to sweeping along hyperplanes.

For viscous simulations, the viscous contribution to the spectral radius K is
used, and everything else remains the same.
 */
double procBlock::LUSGS(const vector<vector3d<int>> &reorder,
                        multiArray3d<genArray> &x,
                        const multiArray3d<genArray> &solTimeMmN,
                        const multiArray3d<genArray> &solDeltaNm1,
                        const idealGas &eqnState, const input &inp,
                        const sutherland &suth, const turbModel *turb) const {
  // reorder -- order of cells to visit (this should be ordered in hyperplanes)
  // x -- correction - added to solution at time n to get to time n+1 (assumed
  // to be zero to start)
  // solTimeMmn -- solution at time m minus n
  // solDeltaNm1 -- solution at time n minus solution at time n-1
  // eqnState -- equation of state_
  // inp -- all input variables
  // suth -- method to get temperature varying viscosity (Sutherland's law)
  // turb -- turbulence model

  double thetaInv = 1.0 / inp.Theta();

  // initialize genArray to zero
  genArray initial(0.0);

  // initialize L and U matrices
  multiArray3d<genArray> U(this->NumI(), this->NumJ(), this->NumK(),
                           initial);
  multiArray3d<genArray> L(this->NumI(), this->NumJ(), this->NumK(),
                           initial);

  //--------------------------------------------------------------------
  // forward sweep over all physical cells
  for (int ii = 0; ii < this->NumCells(); ii++) {
    // indices for variables without ghost cells
    int ip = reorder[ii].X();
    int jp = reorder[ii].Y();
    int kp = reorder[ii].Z();
    // indices for variables with ghost cells
    int ig = reorder[ii].X() + numGhosts_;
    int jg = reorder[ii].Y() + numGhosts_;
    int kg = reorder[ii].Z() + numGhosts_;

    // if i lower diagonal cell is in physical location there is a contribution
    // from it
    if (this->IsPhysical(ip - 1, jp, kp, false)) {
      // at given face location, call function to calculate spectral radius,
      // since values are constant throughout cell, cell center values are used
      double specRad =
          CellSpectralRadius(fAreaI_(ig - 1, jg, kg),
                             fAreaI_(ig, jg, kg),
                             state_(ig - 1, jg, kg).
                             UpdateWithConsVars(eqnState,
                                                x(ip - 1, jp, kp)), eqnState);

      // if viscous add viscous contribution to spectral radius
      if (inp.EquationSet() != "euler") {
        specRad +=
            ViscCellSpectralRadius(fAreaI_(ig - 1, jg, kg),
                                   fAreaI_(ig, jg, kg),
                                   state_(ig - 1, jg, kg).
                                   UpdateWithConsVars(eqnState,
                                                      x(ip - 1, jp, kp)),
                                   eqnState, suth, vol_(ig - 1, jg, kg),
                                   turb->EddyViscNoLim(state_(ig - 1, jg, kg)));
      }

      // at given face location, call function to calculate convective flux
      // change
      genArray fluxChange = ConvectiveFluxUpdate(
          state_(ig - 1, jg, kg), eqnState, this->FAreaUnitI(ig, jg, kg),
          x(ip - 1, jp, kp));

      // update L matrix
      L(ip, jp, kp) = L(ip, jp, kp) + 0.5 *
          (this->FAreaMagI(ig, jg, kg) * fluxChange + inp.MatrixRelaxation() *
           specRad * x(ip - 1, jp, kp));
    }
    // if j lower diagonal cell is in physical location there is a contribution
    // from it
    if (this->IsPhysical(ip, jp - 1, kp, false)) {
      // at given face location, call function to calculate spectral radius,
      // since values are constant throughout cell, cell center values are used
      double specRad =
          CellSpectralRadius(fAreaJ_(ig, jg - 1, kg),
                             fAreaJ_(ig, jg, kg),
                             state_(ig, jg - 1, kg).
                             UpdateWithConsVars(eqnState,
                                                x(ip, jp - 1, kp)), eqnState);

      // if viscous add viscous contribution to spectral radius
      if (inp.EquationSet() != "euler") {
        specRad +=
            ViscCellSpectralRadius(
                fAreaJ_(ig, jg - 1, kg), fAreaJ_(ig, jg, kg),
                state_(ig, jg - 1, kg).
                UpdateWithConsVars(eqnState, x(ip, jp - 1, kp)),
                eqnState, suth, vol_(ig, jg - 1, kg),
                turb->EddyViscNoLim(state_(ig, jg - 1, kg)));
      }

      // at given face location, call function to calculate convective flux
      // change
      genArray fluxChange = ConvectiveFluxUpdate(
          state_(ig, jg - 1, kg), eqnState, this->FAreaUnitJ(ig, jg, kg),
          x(ip, jp - 1, kp));

      // update L matrix
      L(ip, jp, kp) = L(ip, jp, kp) + 0.5 *
          (this->FAreaMagJ(ig, jg, kg) * fluxChange + inp.MatrixRelaxation() *
           specRad * x(ip, jp - 1, kp));
    }
    // if k lower diagonal cell is in physical location there is a contribution
    // from it
    if (this->IsPhysical(ip, jp, kp - 1, false)) {
      // at given face location, call function to calculate spectral radius,
      // since values are constant throughout cell, cell center values are used
      double specRad =
          CellSpectralRadius(fAreaK_(ig, jg, kg - 1),
                             fAreaK_(ig, jg, kg),
                             state_(ig, jg, kg - 1).
                             UpdateWithConsVars(eqnState,
                                                x(ip, jp, kp - 1)), eqnState);

      // if viscous add viscous contribution to spectral radius
      if (inp.EquationSet() != "euler") {
        specRad +=
            ViscCellSpectralRadius(
                fAreaK_(ig, jg, kg - 1), fAreaK_(ig, jg, kg),
                state_(ig, jg, kg - 1).
                UpdateWithConsVars(eqnState, x(ip, jp, kp - 1)),
                eqnState, suth, vol_(ig, jg, kg - 1),
                turb->EddyViscNoLim(state_(ig, jg, kg - 1)));
      }

      // at given face location, call function to calculate convective flux
      // change
      genArray fluxChange = ConvectiveFluxUpdate(
          state_(ig, jg, kg - 1), eqnState, this->FAreaUnitK(ig, jg, kg),
          x(ip, jp, kp - 1));

      // update L matrix
      L(ip, jp, kp) = L(ip, jp, kp) + 0.5 *
          (this->FAreaMagK(ig, jg, kg) * fluxChange + inp.MatrixRelaxation() *
           specRad * x(ip, jp, kp - 1));
    }

    // add dual time stepping contribution to main diagonal
    double diagTimeVol = (vol_(ig, jg, kg) * (1.0 + inp.Zeta())) /
                         (dt_(ip, jp, kp) * inp.Theta());
    if (inp.DualTimeCFL() > 0.0) {  // use dual time stepping
      double tau = avgWaveSpeed_(ip, jp, kp) /
                   inp.DualTimeCFL();  // equal to volume / tau
      diagTimeVol += tau;
    }

    double AiiInv = 1.0 / ((avgWaveSpeed_(ip, jp, kp) + diagTimeVol) *
                           inp.MatrixRelaxation());

    // calculate intermediate update
    // normal at lower boundaries needs to be reversed, so add instead
    // of subtract L
    x(ip, jp, kp) = AiiInv * (-1.0 * thetaInv * residual_(ip, jp, kp) -
                              solDeltaNm1(ip, jp, kp) - solTimeMmN(ip, jp, kp) +
                              L(ip, jp, kp));
  }  // end forward sweep

  //----------------------------------------------------------------------
  // backward sweep over all physical cells
  for (int ii = this->NumCells() - 1; ii >= 0; ii--) {
    // indices for variables without ghost cells
    int ip = reorder[ii].X();
    int jp = reorder[ii].Y();
    int kp = reorder[ii].Z();
    // indices for variables with ghost cells
    int ig = reorder[ii].X() + numGhosts_;
    int jg = reorder[ii].Y() + numGhosts_;
    int kg = reorder[ii].Z() + numGhosts_;

    // if i upper diagonal cell is in physical location there is a contribution
    // from it
    if (this->IsPhysical(ip + 1, jp, kp, false)) {
      // at given face location, call function to calculate spectral radius,
      // since values are constant throughout cell, cell center values are used
      double specRad =
          CellSpectralRadius(fAreaI_(ig + 2, jg, kg),
                             fAreaI_(ig + 1, jg, kg),
                             state_(ig + 1, jg, kg).
                             UpdateWithConsVars(eqnState,
                                                x(ip + 1, jp, kp)), eqnState);

      if (inp.EquationSet() != "euler") {  // viscous
        specRad +=
            ViscCellSpectralRadius(fAreaI_(ig + 2, jg, kg),
                                   fAreaI_(ig + 1, jg, kg),
                                   state_(ig + 1, jg, kg).
                                   UpdateWithConsVars(eqnState,
                                                      x(ip + 1, jp, kp)),
                                   eqnState, suth, vol_(ig + 1, jg, kg),
                                   turb->EddyViscNoLim(state_(ig + 1, jg, kg)));
      }

      // at given face location, call function to calculate convective flux
      // change
      genArray fluxChange = ConvectiveFluxUpdate(
          state_(ig + 1, jg, kg), eqnState, this->FAreaUnitI(ig + 1, jg, kg),
          x(ip + 1, jp, kp));

      // update U matrix
      U(ip, jp, kp) = U(ip, jp, kp) + 0.5 *
          (this->FAreaMagI(ig + 1, jg, kg) * fluxChange -
           inp.MatrixRelaxation() * specRad * x(ip + 1, jp, kp));
    }
    // if j upper diagonal cell is in physical location there is a contribution
    // from it
    if (this->IsPhysical(ip, jp + 1, kp, false)) {
      // at given face location, call function to calculate spectral radius,
      // since values are constant throughout cell, cell center values are used
      double specRad =
          CellSpectralRadius(fAreaJ_(ig, jg + 2, kg),
                             fAreaJ_(ig, jg + 1, kg),
                             state_(ig, jg + 1, kg).
                             UpdateWithConsVars(eqnState,
                                                x(ip, jp + 1, kp)), eqnState);

      if (inp.EquationSet() != "euler") {  // viscous
        specRad +=
            ViscCellSpectralRadius(fAreaJ_(ig, jg + 2, kg),
                                   fAreaJ_(ig, jg + 1, kg),
                                   state_(ig, jg + 1, kg).
                                   UpdateWithConsVars(eqnState,
                                                      x(ip, jp + 1, kp)),
                                   eqnState, suth, vol_(ig, jg + 1, kg),
                                   turb->EddyViscNoLim(state_(ig, jg + 1, kg)));
      }

      // at given face location, call function to calculate convective flux
      // change
      genArray fluxChange = ConvectiveFluxUpdate(
          state_(ig, jg + 1, kg), eqnState, this->FAreaUnitJ(ig, jg + 1, kg),
          x(ip, jp + 1, kp));

      // update U matrix
      U(ip, jp, kp) = U(ip, jp, kp) + 0.5 *
          (this->FAreaMagJ(ig, jg + 1, kg) * fluxChange -
           inp.MatrixRelaxation() * specRad * x(ip, jp + 1, kp));
    }
    // if k upper diagonal cell is in physical location there is a contribution
    // from it
    if (this->IsPhysical(ip, jp, kp + 1, false)) {
      // at given face location, call function to calculate spectral radius,
      // since values are constant throughout cell, cell center values are used
      double specRad =
          CellSpectralRadius(fAreaK_(ig, jg, kg + 2),
                             fAreaK_(ig, jg, kg + 1),
                             state_(ig, jg, kg + 1).
                             UpdateWithConsVars(eqnState,
                                                x(ip, jp, kp + 1)), eqnState);

      if (inp.EquationSet() != "euler") {  // viscous
        specRad +=
            ViscCellSpectralRadius(fAreaK_(ig, jg, kg + 2),
                                   fAreaK_(ig, jg, kg + 1),
                                   state_(ig, jg, kg + 1).
                                   UpdateWithConsVars(eqnState,
                                                      x(ip, jp, kp + 1)),
                                   eqnState, suth, vol_(ig, jg, kg + 1),
                                   turb->EddyViscNoLim(state_(ig, jg, kg + 1)));
      }

      // at given face location, call function to calculate convective flux
      // change
      genArray fluxChange = ConvectiveFluxUpdate(
          state_(ig, jg, kg + 1), eqnState, this->FAreaUnitK(ig, jg, kg + 1),
          x(ip, jp, kp + 1));

      // update U matrix
      U(ip, jp, kp) = U(ip, jp, kp) + 0.5 *
          (this->FAreaMagK(ig, jg, kg + 1) * fluxChange -
           inp.MatrixRelaxation() * specRad * x(ip, jp, kp + 1));
    }

    // add dual time stepping contribution to main diagonal
    double diagTimeVol = (vol_(ig, jg, kg) * (1.0 + inp.Zeta())) /
                         (dt_(ip, jp, kp) * inp.Theta());
    if (inp.DualTimeCFL() > 0.0) {  // use dual time stepping
      double tau = avgWaveSpeed_(ip, jp, kp) /
                   inp.DualTimeCFL();  // equal to volume / tau
      diagTimeVol += tau;
    }

    double AiiInv = 1.0 / ((avgWaveSpeed_(ip, jp, kp) + diagTimeVol) *
                           inp.MatrixRelaxation());

    // calculate update
    x(ip, jp, kp) = x(ip, jp, kp) - AiiInv * U(ip, jp, kp);
  }  // end backward sweep

  //-------------------------------------------------------------------
  // calculate residual
  // initialize LUSGS residual vector
  genArray l2Resid(0.0);

  // loop over physical cells, can use natural order for speed
  for (struct {int p; int g;} kk = {0, numGhosts_}; kk.p <
           this->NumK(); kk.g++, kk.p++) {
    for (struct {int p; int g;} jj = {0, numGhosts_}; jj.p <
             this->NumJ(); jj.g++, jj.p++) {
      for (struct {int p; int g;} ii = {0, numGhosts_}; ii.p <
               this->NumI(); ii.g++, ii.p++) {
        // calclate dual time stepping contribution
        double diagTimeVol = (vol_(ii.g, jj.g, kk.g) * (1.0 + inp.Zeta())) /
            (dt_(ii.p, jj.p, kk.p) * inp.Theta());
        if (inp.DualTimeCFL() > 0.0) {  // use dual time stepping
          double tau = avgWaveSpeed_(ii.p, jj.p, kk.p) /
              inp.DualTimeCFL();  // equal to volume / tau
          diagTimeVol += tau;
        }

        double Aii = (avgWaveSpeed_(ii.p, jj.p, kk.p) + diagTimeVol) *
            inp.MatrixRelaxation();

        // normal at lower boundaries needs to be reversed, so add instead of
        // subtract L
        genArray resid = -1.0 * thetaInv * residual_(ii.p, jj.p, kk.p) +
            solDeltaNm1(ii.p, jj.p, kk.p) + solTimeMmN(ii.p, jj.p, kk.p) - Aii *
            x(ii.p, jj.p, kk.p) + L(ii.p, jj.p, kk.p) - U(ii.p, jj.p, kk.p);
        l2Resid = l2Resid + resid * resid;
      }
    }
  }
  return l2Resid.Sum();
}

/*Function to return the inviscid spectral radius for one direction (i, j, or k)
given a cell state, equation of state, and 2 face area vectors

L = 0.5 * (A1 + A2) * (|Vn| + SoS)

In the above equation L is the spectral radius in either the i, j, or k
direction. A1 and A2 are the two face areas in that direction. Vn is the
cell velocity normal to that direction. SoS is the speed of sound at the cell
 */
double CellSpectralRadius(const unitVec3dMag<double> &fAreaL,
                          const unitVec3dMag<double> &fAreaR,
                          const primVars &state, const idealGas &eqnState) {
  // fAreaL -- face area of lower face in either i, j, or k direction
  // fAreaR -- face area of upper face in either i, j, or k direction
  // state -- state at cell center (primative)
  // eqnState -- equation of state

  // normalize face areas
  vector3d<double> normAvg = (0.5 * (fAreaL.UnitVector() +
                                     fAreaR.UnitVector())).Normalize();
  double fMag = 0.5 * (fAreaL.Mag() + fAreaR.Mag());  // average area magnitude

  // return spectral radius
  return (fabs(state.Velocity().DotProd(normAvg)) + state.SoS(eqnState)) *
         fMag;
}

/*Function to calculate the viscous spectral radius for one direction (i, j, or
k).

L = max(4/(3*rho), g/rho) * mu/Pr * A^2 / V

In the above equation L is the viscous spectral radius for a given direction (i,
j, or k). Rho is the density at the cell center. G is gamma, mu is viscosity,
and Pr is the Prandtl number (all at the cell center). A is the average face area
of the given direction (i, j, k), and V is the cell volume. This implementation
comes from Blazek.
 */
double ViscCellSpectralRadius(const unitVec3dMag<double> &fAreaL,
                              const unitVec3dMag<double> &fAreaR,
                              const primVars &state, const idealGas &eqnState,
                              const sutherland &suth, const double &vol,
                              const double &eddyVisc) {
  // fAreaL -- face area of lower face in either i, j, or k direction
  // fAreaR -- face area of upper face in either i, j, or k direction
  // state -- state at cell center (primative)
  // eqnState -- equation of state
  // suth -- method to the temperature varying visosity and Prandtl number
  // (Sutherland's law)
  // vol -- cell volume

  double fMag = 0.5 * (fAreaL.Mag() + fAreaR.Mag());  // average area magnitude
  double maxTerm =
      max(4.0 / (3.0 * state.Rho()), eqnState.Gamma() / state.Rho());
  double mu = (suth.Viscosity(state.Temperature(eqnState)) +
               eddyVisc) * suth.NondimScaling();  // viscosity at cell center
  double viscTerm = mu / eqnState.Prandtl();

  // return viscous spectral radius
  return maxTerm * viscTerm * fMag * fMag / vol;
}

// function to reconstruct cell variables to the face using central
// differences
template <typename T>
T FaceReconCentral(const T &velU, const T &velD, const vector3d<double> &pU,
                   const vector3d<double> &pD, const vector3d<double> &pF) {
  // velU -- velocity at the cell center of the upwind cell
  // velD -- velocity at the cell center of the downwind cell
  // pU -- position of the cell center of the upwind cell
  // pD -- position of the cell center of the downwind cell
  // pF -- position of the face center of the face on which the reconstruction
  // is happening

  // distance from cell center to cell center
  double cen2cen = pU.Distance(pD);
  // distance from upwind cell center to cell face
  double up2face = pU.Distance(pF);

  // reconstruct with central difference
  return velD * (up2face / cen2cen) + velU * (1.0 - (up2face / cen2cen));
}

/* Function to pad a vector with a specified number of ghost cells
           ___ ___ ___ ___ ___ ___ ___ ___
          | E | E | G | G | G | G | E | E |
          |___|___|___|___|___|___|___|___|
          | E | E | G | G | G | G | E | E |
          |___|___|___|___|___|___|___|___|
          | G | G | X | X | X | X | G | G |
          |___|___|___|___|___|___|___|___|
          | G | G | X | X | X | X | G | G |
          |___|___|___|___|___|___|___|___|
          | E | E | G | G | G | G | E | E |
          |___|___|___|___|___|___|___|___|
          | E | E | G | G | G | G | E | E |
          |___|___|___|___|___|___|___|___|

In the above diagram, the cells marked with an "X" represent physical cells. The
entire diagram represents the block (in 2D) padded with 2 layers of ghost cells.
The cells marked with "G" are regualar ghost cells. The cells marked with "E" are
ghost cells located along one of the 12 edges that form a plot3d block. In 3D
there are also "corner" cells located at the 8 corners that form the plot3d block.
These cells are not used though. There is a place in the vector for them to make
accessing the padded vector of cells the same as for a plot3d block without ghost
cells.
*/
template <typename T>
multiArray3d<T> PadWithGhosts(const multiArray3d<T> &var,
                              const int &numGhosts) {
  // var -- vector of variables to pad (no ghost cells included)
  // numGhosts -- number of layers of ghost cells to pad var with

  // initialize added array
  multiArray3d<T> padBlk(var.NumI() + 2 * numGhosts, var.NumJ() + 2 * numGhosts,
                         var.NumK() + 2 * numGhosts);

  padBlk.Insert(numGhosts, padBlk.NumI() - numGhosts - 1,
                numGhosts, padBlk.NumJ() - numGhosts - 1,
                numGhosts, padBlk.NumK() - numGhosts - 1, var);
  return padBlk;
}

/* Function to calculate the velocity gradient at the cell center using the
Green-Gauss method

dU/dxj = (Sum)    U * Aij  / V        (j=1,2,3)
       i=1,nfaces

The above equation shows how the gradient of a scalar is calculated using the
Green-Gauss method. U is a scalar. Aij is the area at face i (component j).
V is the volume of the control volume. X is the cartesian direction with
j indicating the component. The convention is for the area vectors to point out
of the control volume.
 */
tensor<double> CalcVelGradGG(
    const vector3d<double> &vil, const vector3d<double> &viu,
    const vector3d<double> &vjl, const vector3d<double> &vju,
    const vector3d<double> &vkl, const vector3d<double> &vku,
    const vector3d<double> &ail, const vector3d<double> &aiu,
    const vector3d<double> &ajl, const vector3d<double> &aju,
    const vector3d<double> &akl, const vector3d<double> &aku,
    const double &vol) {
  // vil -- velocity vector at the i-lower face of the cell at which the
  // velocity gradient is being calculated
  // viu -- velocity vector at the i-upper face of the cell at which the
  // velocity gradient is being calculated
  // vjl -- velocity vector at the j-lower face of the cell at which the
  // velocity gradient is being calculated
  // vju -- velocity vector at the j-upper face of the cell at which the
  // velocity gradient is being calculated
  // vkl -- velocity vector at the k-lower face of the cell at which the
  // velocity gradient is being calculated
  // vku -- velocity vector at the k-upper face of the cell at which the
  // velocity gradient is being calculated

  // ail -- area vector at the lower i-face of the cell at which the velocity
  // gradient is being calculated
  // aiu -- area vector at the upper i-face of the cell at which the velocity
  // gradient is being calculated
  // ajl -- area vector at the lower j-face of the cell at which the velocity
  // gradient is being calculated
  // aju -- area vector at the upper j-face of the cell at which the velocity
  // gradient is being calculated
  // akl -- area vector at the lower k-face of the cell at which the velocity
  // gradient is being calculated
  // aku -- area vector at the upper k-face of the cell at which the velocity
  // gradient is being calculated

  // vol -- cell volume

  tensor<double> temp;
  double invVol = 1.0 / vol;

  // define velocity gradient tensor
  // convention is for area vector to point out of cell, so lower values are
  // negative, upper are positive
  temp.SetXX(invVol *
             (viu.X() * aiu.X() - vil.X() * ail.X() + vju.X() * aju.X() -
              vjl.X() * ajl.X() + vku.X() * aku.X() - vkl.X() * akl.X()));
  temp.SetXY(invVol *
             (viu.Y() * aiu.X() - vil.Y() * ail.X() + vju.Y() * aju.X() -
              vjl.Y() * ajl.X() + vku.Y() * aku.X() - vkl.Y() * akl.X()));
  temp.SetXZ(invVol *
             (viu.Z() * aiu.X() - vil.Z() * ail.X() + vju.Z() * aju.X() -
              vjl.Z() * ajl.X() + vku.Z() * aku.X() - vkl.Z() * akl.X()));

  temp.SetYX(invVol *
             (viu.X() * aiu.Y() - vil.X() * ail.Y() + vju.X() * aju.Y() -
              vjl.X() * ajl.Y() + vku.X() * aku.Y() - vkl.X() * akl.Y()));
  temp.SetYY(invVol *
             (viu.Y() * aiu.Y() - vil.Y() * ail.Y() + vju.Y() * aju.Y() -
              vjl.Y() * ajl.Y() + vku.Y() * aku.Y() - vkl.Y() * akl.Y()));
  temp.SetYZ(invVol *
             (viu.Z() * aiu.Y() - vil.Z() * ail.Y() + vju.Z() * aju.Y() -
              vjl.Z() * ajl.Y() + vku.Z() * aku.Y() - vkl.Z() * akl.Y()));

  temp.SetZX(invVol *
             (viu.X() * aiu.Z() - vil.X() * ail.Z() + vju.X() * aju.Z() -
              vjl.X() * ajl.Z() + vku.X() * aku.Z() - vkl.X() * akl.Z()));
  temp.SetZY(invVol *
             (viu.Y() * aiu.Z() - vil.Y() * ail.Z() + vju.Y() * aju.Z() -
              vjl.Y() * ajl.Z() + vku.Y() * aku.Z() - vkl.Y() * akl.Z()));
  temp.SetZZ(invVol *
             (viu.Z() * aiu.Z() - vil.Z() * ail.Z() + vju.Z() * aju.Z() -
              vjl.Z() * ajl.Z() + vku.Z() * aku.Z() - vkl.Z() * akl.Z()));

  return temp;
}

/* Function to calculate the gradient of a scalar at the cell center using the
Green-Gauss method

dU/dxj = (Sum)    U * Aij  / V        (j=1,2,3)
       i=1,nfaces

The above equation shows how the gradient of a scalar is calculated using the
Green-Gauss method. U is a scalar. Aij is the area at face i (component j).
V is the volume of the control volume. X is the cartesian direction with
j indicating the component. The convention is for the area vectors to point out
of the control volume.
 */
vector3d<double> CalcScalarGradGG(
    const double &til, const double &tiu, const double &tjl, const double &tju,
    const double &tkl, const double &tku, const vector3d<double> &ail,
    const vector3d<double> &aiu, const vector3d<double> &ajl,
    const vector3d<double> &aju, const vector3d<double> &akl,
    const vector3d<double> &aku, const double &vol) {
  // til -- scalar value at the lower face of the cell at which the scalar
  // gradient is being calculated
  // tiu -- scalar value at the upper face of the cell at which the scalar
  // gradient is being calculated
  // tjl -- scalar value at the lower face of the cell at which the scalar
  // gradient is being calculated
  // tju -- scalar value at the upper face of the cell at which the scalar
  // gradient is being calculated
  // tkl -- scalar value at the lower face of the cell at which the scalar
  // gradient is being calculated
  // tku -- scalar value at the upper face of the cell at which the scalar
  // gradient is being calculated

  // ail -- area vector at the lower face of the cell at which the scalar
  // gradient is being calculated
  // aiu -- area vector at the upper face of the cell at which the scalar
  // gradient is being calculated
  // ajl -- area vector at the lower face of the cell at which the scalar
  // gradient is being calculated
  // aju -- area vector at the upper face of the cell at which the scalar
  // gradient is being calculated
  // akl -- area vector at the lower face of the cell at which the scalar
  // gradient is being calculated
  // aku -- area vector at the upper face of the cell at which the scalar
  // gradient is being calculated

  // vol -- cell volume

  vector3d<double> temp;
  double invVol = 1.0 / vol;

  // define scalar gradient vector
  // convention is for area vector to point out of cell, so lower values are
  // negative, upper are positive
  temp.SetX(invVol * (tiu * aiu.X() - til * ail.X() + tju * aju.X() -
                      tjl * ajl.X() + tku * aku.X() - tkl * akl.X()));
  temp.SetY(invVol * (tiu * aiu.Y() - til * ail.Y() + tju * aju.Y() -
                      tjl * ajl.Y() + tku * aku.Y() - tkl * akl.Y()));
  temp.SetZ(invVol * (tiu * aiu.Z() - til * ail.Z() + tju * aju.Z() -
                      tjl * ajl.Z() + tku * aku.Z() - tkl * akl.Z()));

  return temp;
}

/* Function to calculate the viscous fluxes on the i-faces. All phyiscal
(non-ghost) i-faces are looped over. The left and right states are
calculated, and then the flux at the face is calculated. The flux at the
face contributes to the residual of the cells to the left and right of the
face. This contribution from the flux is added to the residuals and the wave
speed is accumulated as well.
  ___________________________
  |            |            |
  |            |            |
  |   Ui       -->   Ui+1   |
  |            |            |
  |____________|____________|
Ui-1/2       Ui+1/2       Ui+3/2

Using the above diagram, the flux is calculated at face Ui+1/2. Since the area
vector at the face always points from lower indices to higher indices it points
from Ui to Ui+1. For the residual calculation the convention is for the area
vector to point out of the cell. Therefore it is in the correct position for the
residual at Ui, but the sign needs to be flipped when adding the contribution of
the flux to the residual at Ui+1.

The spectral radius in the i-direction is also calculated. Since this is done on
a cell basis instead of a face bases, it is only calculated for the upper cell
(Ui+1 in this case). The spectral radius is added to the average wave speed
variable and is eventually used in the time step calculation if the time step
isn't explicitly specified.

The velocity and temperature gradients are calculated at each cell face by
constructing an alternative control volume centered around that face as show below.
  ___________________________
  |            |            |
  |            |            |
  |   Ui,j+1   |   Ui+1,j+1 |
  |            |            |
  |_____*******|*******_____|
  |     *      |      *     |
  |     *      |      *     |
  |   Ui,j     |   Ui+1,j   |
  |     *      |      *     |
  |_____*******|*******_____|
  |            |            |
  |            |            |
  |   Ui,j-1   |   Ui+1,j-1 |
  |            |            |
  |____________|____________|

The above diagram shows a 2D schematic of how the gradient calculation is done.
In this example the gradient is being calculated at the face between cell Ui,j
and Ui+1,j. The dashes represent the grid cells, and the astrisks represent the
alternative control volume. The grid cells themselves cannot be used as the
control volume for the gradients (averaging values at adjacent cells to get
gradients at the face) because this leads to odd/even decoupling. The face areas,
volumes, and states at the center_ of the faces are needed for the alternative
control volume. The left and right sides of the alternate control volume pass
through the center_ of cells Ui,j and Ui+1,j respectively. Therefore these values
are used for the face states. The left and right face areas are calculated as the
average of the face areas of the cells that they split. For example, the left face
area would be calculated as 0.5 * (Ai+1/2,j + Ai-1/2,j). The top and bottom sides
of the alternative control volume pass through 4 cells each. Therefore the value
of the state at the face center is the average of these four states. For example
the state at the top face is calculated as 0.25 * (Ui,j + Ui+1,j + Ui,j+1 +
Ui+1,j+1). The face areas of the top and bottom sides are calculated as the
average of the 2 face areas that each one passes through. For example, the top
face area is calculated as 0.5 * (Ai,j+1/2 + Ai+1,j+1/2). In three dimensions
each gradient calculation touches the values at 10 cells (6 shown and 4 more
in/out of the page). The stencil for the gradients of all faces in a cell
touches 15 cells. The gradient calculation with this stencil uses the "edge"
ghost cells, but not the "corner" ghost cells.
*/
void procBlock::CalcViscFluxI(const sutherland &suth, const idealGas &eqnState,
                              const input &inp, const gradients &grads,
                              const turbModel *turb) {
  // suth -- method to get viscosity as a function of temperature (Sutherland's
  // law)
  // eqnState -- equation of state
  // inp -- all input variables
  // grads -- class holding gradients at face for velocity, temperature, tke,
  // and omega
  // turb -- turbulence model

  // coefficient for viscous spectral radii
  double vCoeff = 1.0;

  // loop over all physical i-faces
  // in struct p is for physical index, g is for index with ghosts
  for (struct {int p; int g;} kk = {0, numGhosts_}; kk.g <
           fAreaI_.NumK() - numGhosts_; kk.g++, kk.p++) {
    for (struct {int p; int g;} jj = {0, numGhosts_}; jj.g <
             fAreaI_.NumJ() - numGhosts_; jj.g++, jj.p++) {
      for (struct {int p; int g;} ii = {0, numGhosts_}; ii.g <
               fAreaI_.NumI() - numGhosts_; ii.g++, ii.p++) {
        // Get velocity at face
        vector3d<double> vel =
            FaceReconCentral(state_(ii.g - 1, jj.g, kk.g).Velocity(),
                             state_(ii.g, jj.g, kk.g).Velocity(),
                             center_(ii.g - 1, jj.g, kk.g),
                             center_(ii.g, jj.g, kk.g),
                             fCenterI_(ii.g, jj.g, kk.g));

        // Get state at face
        primVars state =
            FaceReconCentral(state_(ii.g - 1, jj.g, kk.g),
                             state_(ii.g, jj.g, kk.g),
                             center_(ii.g - 1, jj.g, kk.g),
                             center_(ii.g, jj.g, kk.g),
                             fCenterI_(ii.g, jj.g, kk.g));

        // Get viscosity at face
        double mu =
            FaceReconCentral(
                suth.EffectiveViscosity(state_(ii.g - 1, jj.g, kk.g).
                                        Temperature(eqnState)),
                suth.EffectiveViscosity(state_(ii.g, jj.g, kk.g).
                                        Temperature(eqnState)),
                center_(ii.g - 1, jj.g, kk.g), center_(ii.g, jj.g, kk.g),
                fCenterI_(ii.g, jj.g, kk.g));

        double eddyVisc =
            FaceReconCentral(
            turb->EddyVisc(state_(ii.g - 1, jj.g, kk.g),
                           grads.VelGradI(ii.p, jj.p, kk.p), suth),
            turb->EddyVisc(state_(ii.g, jj.g, kk.g),
                           grads.VelGradI(ii.p, jj.p, kk.p), suth),
            center_(ii.g - 1, jj.g, kk.g), center_(ii.g, jj.g, kk.g),
            fCenterI_(ii.g, jj.g, kk.g));
        eddyVisc *= suth.NondimScaling();  // effective viscosity

        // calculate viscous flux
        vector3d<double> tkeGrad, omegaGrad;
        if (inp.IsTurbulent()) {
          tkeGrad = grads.TkeGradI(ii.p, jj.p, kk.p);
          omegaGrad = grads.OmegaGradI(ii.p, jj.p, kk.p);
        }
        viscousFlux tempViscFlux(grads.VelGradI(ii.p, jj.p, kk.p), vel, mu,
                                 eddyVisc, suth, eqnState,
                                 grads.TempGradI(ii.p, jj.p, kk.p),
                                 this->FAreaUnitI(ii.g, jj.g, kk.g), tkeGrad,
                                 omegaGrad, turb, state);

        // area vector points from left to right, so add to left cell, subtract
        // from right cell but viscous fluxes are subtracted from inviscid
        // fluxes, so sign is reversed
        // at left boundary there is no left cell to add to
        if (ii.g > numGhosts_) {
          this->AddToResidual(-1.0 * tempViscFlux *
                                this->FAreaMagI(ii.g, jj.g, kk.g),
                                ii.p - 1, jj.p, kk.p);
        }
        // at right boundary ther eis to right cell to add to
        if (ii.g < fAreaI_.NumI() - numGhosts_ - 1) {
          this->AddToResidual(tempViscFlux *
                                this->FAreaMagI(ii.g, jj.g, kk.g),
                                ii.p, jj.p, kk.p);

          // calculate component of wave speed. This is done on a cell by cell
          // basis, so only at the upper faces
          avgWaveSpeed_(ii.p, jj.p, kk.p) += vCoeff *
              ViscCellSpectralRadius(
                  fAreaI_(ii.g, jj.g, kk.g),
                  fAreaI_(ii.g + 1, jj.g, kk.g),
                  state_(ii.g, jj.g, kk.g), eqnState, suth,
                  vol_(ii.g, jj.g, kk.g),
                  turb->EddyVisc(state_(ii.g, jj.g, kk.g),
                                 grads.VelGradI(ii.p, jj.p, kk.p), suth));
        }
      }
    }
  }
}

/* Function to calculate the viscous fluxes on the j-faces. All phyiscal
(non-ghost) j-faces are looped over. The left and right states are calculated,
and then the flux at the face is calculated. The flux at the face contributes
to the residual of the cells to the left and right of the face. This
contribution from the flux is added to the residuals and the wave speed is
accumulated as well.
  ___________________________
  |            |            |
  |            |            |
  |   Uj       -->   Uj+1   |
  |            |            |
  |____________|____________|
Uj-1/2       Uj+1/2       Uj+3/2

Using the above diagram, the flux is calculated at face Uj+1/2. Since the area
vector at the face always points from lower indices to higher indices it points
from Uj to Uj+1. For the residual calculation the convention is for the area
vector to point out of the cell. Therefore it is in the correct position for the
residual at Uj, but the sign needs to be flipped when adding the contribution of
the flux to the residual at Uj+1.

The spectral radius in the j-direction is also calculated. Since this is done on
a cell basis instead of a face bases, it is only calculated for the upper cell
(Uj+1 in this case). The spectral radius is added to the average wave speed
variable and is eventually used in the time step calculation if the time step
isn't explicitly specified.

The velocity and temperature gradients are calculated at each cell face by
constructing an alternative control volume centered around that face as shown
below.

  ___________________________
  |            |            |
  |            |            |
  |   Ui,j+1   |   Ui+1,j+1 |
  |            |            |
  |_____*******|*******_____|
  |     *      |      *     |
  |     *      |      *     |
  |   Ui,j     |   Ui+1,j   |
  |     *      |      *     |
  |_____*******|*******_____|
  |            |            |
  |            |            |
  |   Ui,j-1   |   Ui+1,j-1 |
  |            |            |
  |____________|____________|

The above diagram shows a 2D schematic of how the gradient calculation is done.
In this example the gradient is being calculated at the face between cell Ui,j
and Ui+1,j. The dashes represent the grid cells, and the astrisks represent the
alternative control volume. The grid cells themselves cannot be used as the
control volume for the gradients (averaging values at adjacent cells to get
gradients at the face) because this leads to odd/even decoupling. The face
areas, volumes, and states at the center_ of the faces are needed for the
alternative control volume. The left and right sides of the alternate control
volume pass through the center of cells Ui,j and Ui+1,j respectively. Therefore
these values are used for the face states. The left and right face areas are
calculated as the average of the face areas of the cells that they split. For
example, the left face area would be calculated as 0.5 * (Ai+1/2,j + Ai-1/2,j).
The top and bottom sides of the alternative control volume pass through 4 cells
each. Therefore the value of the state at the face center is the average of
these four states. For example the state at the top face is calculated as 0.25 *
(Ui,j + Ui+1,j + Ui,j+1, Ui+1,j+1). The face areas of the top and bottom sides
are calculated as the average of the 2 face areas that each one passes through.
For example, the top face area is calculated as 0.5 * (Ai,j+1/2 + Ai+1,j+1/2).
In three dimensions each gradient calculation touches the values at 10 cells
(6 shown and 4 more in/out of the page). The stencil for the gradients of all
faces in a cell touches 15 cells. The gradient calculation with this stencil uses
the "edge" ghost cells, but not the "corner" ghost cells.
*/
void procBlock::CalcViscFluxJ(const sutherland &suth, const idealGas &eqnState,
                              const input &inp, const gradients &grads,
                              const turbModel *turb) {
  // suth -- method to get viscosity as a function of temperature (Sutherland's
  // law)
  // eqnState -- equation of state_
  // inp -- all input variables
  // grads -- class holding gradients at face for velocity, temperature, tke,
  // and omega
  // turb -- turbulence model

  // coefficient for viscous spectral radii
  double vCoeff = 1.0;


  // loop over all physical j-faces
  // in struct p is for physical index, g is for index with ghosts
  for (struct {int p; int g;} kk = {0, numGhosts_}; kk.g <
           fAreaJ_.NumK() - numGhosts_; kk.g++, kk.p++) {
    for (struct {int p; int g;} jj = {0, numGhosts_}; jj.g <
             fAreaJ_.NumJ() - numGhosts_; jj.g++, jj.p++) {
      for (struct {int p; int g;} ii = {0, numGhosts_}; ii.g <
               fAreaJ_.NumI() - numGhosts_; ii.g++, ii.p++) {
        // Get velocity at face
        vector3d<double> vel =
            FaceReconCentral(state_(ii.g, jj.g - 1, kk.g).Velocity(),
                             state_(ii.g, jj.g, kk.g).Velocity(),
                             center_(ii.g, jj.g - 1, kk.g),
                             center_(ii.g, jj.g, kk.g),
                             fCenterJ_(ii.g, jj.g, kk.g));

        // Get velocity at face
        primVars state =
            FaceReconCentral(state_(ii.g, jj.g - 1, kk.g),
                             state_(ii.g, jj.g, kk.g),
                             center_(ii.g, jj.g - 1, kk.g),
                             center_(ii.g, jj.g, kk.g),
                             fCenterJ_(ii.g, jj.g, kk.g));

        // Get viscosity at face
        double mu =
            FaceReconCentral(
                suth.EffectiveViscosity(state_(ii.g, jj.g - 1, kk.g).
                                        Temperature(eqnState)),
                suth.EffectiveViscosity(state_(ii.g, jj.g, kk.g).
                                        Temperature(eqnState)),
                center_(ii.g, jj.g - 1, kk.g), center_(ii.g, jj.g, kk.g),
                fCenterJ_(ii.g, jj.g, kk.g));

        double eddyVisc =
            FaceReconCentral(
                turb->EddyVisc(state_(ii.g, jj.g - 1, kk.g),
                               grads.VelGradJ(ii.p, jj.p, kk.p), suth),
                turb->EddyVisc(state_(ii.g, jj.g, kk.g),
                               grads.VelGradJ(ii.p, jj.p, kk.p), suth),
                             center_(ii.g, jj.g - 1, kk.g),
                             center_(ii.g, jj.g, kk.g),
                             fCenterJ_(ii.g, jj.g, kk.g));
        // effective viscosity (due to nondimensionalization)
        eddyVisc *= suth.NondimScaling();

        // calculate viscous flux
        vector3d<double> tkeGrad, omegaGrad;
        if (inp.IsTurbulent()) {
          tkeGrad = grads.TkeGradJ(ii.p, jj.p, kk.p);
          omegaGrad = grads.OmegaGradJ(ii.p, jj.p, kk.p);
        }
        viscousFlux tempViscFlux(grads.VelGradJ(ii.p, jj.p, kk.p), vel, mu,
                                 eddyVisc, suth, eqnState,
                                 grads.TempGradJ(ii.p, jj.p, kk.p),
                                 this->FAreaUnitJ(ii.g, jj.g, kk.g), tkeGrad,
                                 omegaGrad, turb, state);

        // area vector points from left to right, so add to left cell, subtract
        // from right cell but viscous fluxes are subtracted from inviscid
        // fluxes, so sign is reversed
        // at left boundary there is no left cell to add to
        if (jj.g > numGhosts_) {
          this->AddToResidual(-1.0 * tempViscFlux *
                                this->FAreaMagJ(ii.g, jj.g, kk.g),
                                ii.p, jj.p - 1, kk.p);
        }
        // at right boundary there is no right cell to add to
        if (jj.g < fAreaJ_.NumJ() - numGhosts_ - 1) {
          this->AddToResidual(tempViscFlux *
                                this->FAreaMagJ(ii.g, jj.g, kk.g),
                                ii.p, jj.p, kk.p);

          // calculate component of wave speed. This is done on a cell by cell
          // basis, so only at the upper faces
          avgWaveSpeed_(ii.p, jj.p, kk.p) += vCoeff *
              ViscCellSpectralRadius(
                  fAreaJ_(ii.g, jj.g, kk.g),
                  fAreaJ_(ii.g, jj.g + 1, kk.g),
                  state_(ii.g, jj.g, kk.g), eqnState, suth,
                  vol_(ii.g, jj.g, kk.g),
                  turb->EddyVisc(state_(ii.g, jj.g, kk.g),
                                 grads.VelGradJ(ii.p, jj.p, kk.p), suth));
        }
      }
    }
  }
}

/* Function to calculate the viscous fluxes on the k-faces. All phyiscal
(non-ghost) k-faces are looped over. The left and right states are calculated,
and then the flux at the face is calculated. The flux at the face contributes
to the residual of the cells to the left and right of the face. This
contribution from the flux is added to the residuals and the wave speed is
accumulated as well.
  ___________________________
  |            |            |
  |            |            |
  |   Uk       -->   Uk+1   |
  |            |            |
  |____________|____________|
Uk-1/2       Uk+1/2       Uk+3/2

Using the above diagram, the flux is calculated at face Uk+1/2. Since the area
vector at the face always points from lower indices to higher indices it points
from Uk to Uk+1. For the residual calculation the convention is for the area
vector to point out of the cell. Therefore it is in the correct position for the
residual at Uk, but the sign needs to be flipped when adding the contribution of
the flux to the residual at Uk+1.

The spectral radius in the k-direction is also calculated. Since this is done on
a cell basis instead of a face bases, it is only calculated for the upper cell
(Uk+1 in this case). The spectral radius is added to the average wave speed
variable and is eventually used in the time step calculation if the time step
isn't explicitly specified.

The velocity and temperature gradients are calculated at each cell face by
constructing an alternative control volume centered around that face as shown
below.
  ___________________________
  |            |            |
  |            |            |
  |   Ui,j+1   |   Ui+1,j+1 |
  |            |            |
  |_____*******|*******_____|
  |     *      |      *     |
  |     *      |      *     |
  |   Ui,j     |   Ui+1,j   |
  |     *      |      *     |
  |_____*******|*******_____|
  |            |            |
  |            |            |
  |   Ui,j-1   |   Ui+1,j-1 |
  |            |            |
  |____________|____________|

The above diagram shows a 2D schematic of how the gradient calculation is done.
In this example the gradient is being calculated at the face between cell Ui,j
and Ui+1,j. The dashes represent the grid cells, and the astrisks represent the
alternative control volume. The grid cells themselves cannot be used as the
control volume for the gradients (averaging values at adjacent cells to get
gradients at the face) because this leads to odd/even decoupling. The face
areas, volumes, and states at the center_ of the faces are needed for the
alternative control volume. The left and right sides of the alternate control
volume pass through the center_ of cells Ui,j and Ui+1,j respectively. Therefore
these values are used for the face states. The left and right face areas are
calculated as the average of the face areas of the cells that they split. For
example, the left face area would be calculated as 0.5 * (Ai+1/2,j + Ai-1/2,j).
The top and bottom sides of the alternative control volume pass through 4 cells
each. Therefore the value of the state_ at the face center_ is the average of
these four states. For example the state_ at the top face is calculated as 0.25 *
(Ui,j + Ui+1,j + Ui,j+1, Ui+1,j+1). The face areas of the top and bottom sides
are calculated as the average of the 2 face areas that each one passes through.
For example, the top face area is calculated as 0.5 * (Ai,j+1/2 + Ai+1,j+1/2).
In three dimensions each gradient calculation touches the values at 10 cells
(6 shown and 4 more in/out of the page). The stencil for the gradients of all
faces in a cell touches 15 cells. The gradient calculation with this stencil uses
the "edge" ghost cells, but not the "corner" ghost cells.
*/
void procBlock::CalcViscFluxK(const sutherland &suth, const idealGas &eqnState,
                              const input &inp, const gradients &grads,
                              const turbModel *turb) {
  // suth -- method to get viscosity as a function of temperature (Sutherland's
  // law)
  // eqnState -- equation of state_
  // inp -- all input variables
  // grads -- class holding gradients at face for velocity, temperature, tke,
  // and omega
  // turb -- turbulence model

  // coefficient for viscous spectral radii
  double vCoeff = 1.0;

  // loop over all physical k-faces
  // in struct p is for physical index, g is for index with ghosts
  for (struct {int p; int g;} kk = {0, numGhosts_}; kk.g <
           fAreaK_.NumK() - numGhosts_; kk.g++, kk.p++) {
    for (struct {int p; int g;} jj = {0, numGhosts_}; jj.g <
             fAreaK_.NumJ() - numGhosts_; jj.g++, jj.p++) {
      for (struct {int p; int g;} ii = {0, numGhosts_}; ii.g <
               fAreaK_.NumI() - numGhosts_; ii.g++, ii.p++) {
        // Get velocity at face
        vector3d<double> vel =
            FaceReconCentral(state_(ii.g, jj.g, kk.g - 1).Velocity(),
                             state_(ii.g, jj.g, kk.g).Velocity(),
                             center_(ii.g, jj.g, kk.g - 1),
                             center_(ii.g, jj.g, kk.g),
                             fCenterK_(ii.g, jj.g, kk.g));

        // Get velocity at face
        primVars state =
            FaceReconCentral(state_(ii.g, jj.g, kk.g - 1),
                             state_(ii.g, jj.g, kk.g),
                             center_(ii.g, jj.g, kk.g - 1),
                             center_(ii.g, jj.g, kk.g),
                             fCenterK_(ii.g, jj.g, kk.g));

        // Get viscosity at face
        double mu =
            FaceReconCentral(
                suth.EffectiveViscosity(state_(ii.g, jj.g, kk.g - 1).
                                        Temperature(eqnState)),
                suth.EffectiveViscosity(state_(ii.g, jj.g, kk.g).
                                        Temperature(eqnState)),
                center_(ii.g, jj.g, kk.g - 1), center_(ii.g, jj.g, kk.g),
                fCenterK_(ii.g, jj.g, kk.g));

        double eddyVisc =
            FaceReconCentral(
            turb->EddyVisc(state_(ii.g, jj.g, kk.g - 1),
                           grads.VelGradK(ii.p, jj.p, kk.p), suth),
            turb->EddyVisc(state_(ii.g, jj.g, kk.g),
                           grads.VelGradK(ii.p, jj.p, kk.p), suth),
            center_(ii.g, jj.g, kk.g - 1), center_(ii.g, jj.g, kk.g),
            fCenterK_(ii.g, jj.g, kk.g));
        // effective viscosity (due to nondimensionalization)
        eddyVisc *= suth.NondimScaling();

        // calculate viscous flux
        vector3d<double> tkeGrad, omegaGrad;
        if (inp.IsTurbulent()) {
          tkeGrad = grads.TkeGradK(ii.p, jj.p, kk.p);
          omegaGrad = grads.OmegaGradK(ii.p, jj.p, kk.p);
        }
        viscousFlux tempViscFlux(grads.VelGradK(ii.p, jj.p, kk.p), vel, mu,
                                 eddyVisc, suth, eqnState,
                                 grads.TempGradK(ii.p, jj.p, kk.p),
                                 this->FAreaUnitK(ii.g, jj.g, kk.g), tkeGrad,
                                 omegaGrad, turb, state);

        // area vector points from left to right, so add to left cell, subtract
        // from right cell but viscous fluxes are subtracted from inviscid
        // fluxes, so sign is reversed
        // at left boundary there is no left cell to add to
        if (kk.g > numGhosts_) {
          this->AddToResidual(-1.0 * tempViscFlux *
                                this->FAreaMagK(ii.g, jj.g, kk.g),
                                ii.p, jj.p, kk.p - 1);
        }
        // at right boundary there is no right cell to add to
        if (kk.g < fAreaK_.NumK() - numGhosts_ - 1) {
          this->AddToResidual(tempViscFlux *
                                this->FAreaMagK(ii.g, jj.g, kk.g),
                                ii.p, jj.p, kk.p);

          // calculate component of wave speed. This is done on a cell by cell
          // basis, so only at the upper faces
          avgWaveSpeed_(ii.p, jj.p, kk.p) += vCoeff *
              ViscCellSpectralRadius(
                  fAreaK_(ii.g, jj.g, kk.g),
                  fAreaK_(ii.g, jj.g, kk.g + 1),
                  state_(ii.g, jj.g, kk.g), eqnState, suth,
                  vol_(ii.g, jj.g, kk.g),
                  turb->EddyVisc(state_(ii.g, jj.g, kk.g),
                                 grads.VelGradK(ii.p, jj.p, kk.p), suth));
        }
      }
    }
  }
}

/* Member function to assign geometric quantities such as volume, face area,
cell centroid, and face center_ to ghost cells. This assigns values for
regular ghost cells and "edge" ghost cells. "Corner" cells are left with no
value as they are not used.
           ____ ____ ____ ____ ____ ____ ____ ____
          | E  | E  | G2 | G2 | G2 | G2 | E  | E  |
          |____|____|____|____|____|____|____|____|
          | E  | E  | G1 | G1 | G1 | G1 | E  | E  |
          |____|____|____|____|____|____|____|____|
          | G2 | G1 | X  | X  | X  | X  | G1 | G2 |
          |____|____|____|____|____|____|____|____|
          | G2 | G1 | X  | X  | X  | X  | G1 | G2 |
          |____|____|____|____|____|____|____|____|
          | E  | E  | G1 | G1 | G1 | G1 | E  | E  |
          |____|____|____|____|____|____|____|____|
          | E  | E  | G2 | G2 | G2 | G2 | E  | E  |
          |____|____|____|____|____|____|____|____|

In the above diagram where X represents the physical cells, cells marked G
(regular ghost cells) and E ("edge" ghost cells) are assigned geometric
values. G1 represents the first layer of ghost cells and G2 represents the
second layer.
*/
void procBlock::AssignGhostCellsGeom() {
  // loop over all boundary surfaces
  for (int ii = 0; ii < bc_.NumSurfaces(); ii++) {
    // Get surface boundaries, and adjust them for ghost cells
    int imin = bc_.GetIMin(ii) - 1 + numGhosts_;
    int imax = bc_.GetIMax(ii) - 2 + numGhosts_;
    int jmin = bc_.GetJMin(ii) - 1 + numGhosts_;
    int jmax = bc_.GetJMax(ii) - 2 + numGhosts_;
    int kmin = bc_.GetKMin(ii) - 1 + numGhosts_;
    int kmax = bc_.GetKMax(ii) - 2 + numGhosts_;

    int imaxF = imax + 1;
    int jmaxF = jmax + 1;
    int kmaxF = kmax + 1;

    int g1, g2, i1, i2;  // indices for cells
    int fg1, fg2, fi1, fi2, bnd;  // indices for faces
    if (bc_.GetSurfaceType(ii) == 2) {  // upper i-surface
      g2 = imax + 2;
      g1 = imax + 1;
      i1 = imax;
      i2 = imax - 1;

      fg2 = imax + 3;
      fg1 = imax + 2;
      bnd = imax + 1;
      fi1 = imax;
      fi2 = imax - 1;
    } else if (bc_.GetSurfaceType(ii) == 4) {  // upper j-surface
      g2 = jmax + 2;
      g1 = jmax + 1;
      i1 = jmax;
      i2 = jmax - 1;

      fg2 = jmax + 3;
      fg1 = jmax + 2;
      bnd = jmax + 1;
      fi1 = jmax;
      fi2 = jmax - 1;
    } else if (bc_.GetSurfaceType(ii) == 6) {  // upper k-surface
      g2 = kmax + 2;
      g1 = kmax + 1;
      i1 = kmax;
      i2 = kmax - 1;

      fg2 = kmax + 3;
      fg1 = kmax + 2;
      bnd = kmax + 1;
      fi1 = kmax;
      fi2 = kmax - 1;
    } else {  // lower surface
      g2 = 0;
      g1 = 1;
      i1 = 2;
      i2 = 3;

      fg2 = 0;
      fg1 = 1;
      bnd = 2;
      fi1 = 3;
      fi2 = 4;
    }

    //-----------------------------------------------------------------------
    // Assign ghost geometry for i-surfaces
    // -----------------------------------------------------------------------
    // only supply geometry values for non interblock BCs
    // for interblock do nothing
    if ((bc_.GetSurfaceType(ii) == 1 || bc_.GetSurfaceType(ii) == 2) &&
        bc_.GetBCTypes(ii) != "interblock") {
      // assign volume for first layer of ghost cells
      vol_.Insert(g1, g1, jmin, jmax, kmin, kmax,
                  vol_.Slice(i1, i1, jmin, jmax, kmin, kmax));

      // assign volume for second layer of ghost cells
      // one cell thick - use one cell for both ghost cells
      if (this->NumI() < 2) {
        vol_.Insert(g2, g2, jmin, jmax, kmin, kmax,
                    vol_.Slice(i1, i1, jmin, jmax, kmin, kmax));
      } else {
        vol_.Insert(g2, g2, jmin, jmax, kmin, kmax,
                    vol_.Slice(i2, i2, jmin, jmax, kmin, kmax));
      }

      // assign face areas for first layer
      fAreaI_.Insert(fg1, fg1, jmin, jmax, kmin, kmax,
                     fAreaI_.Slice(fi1, fi1, jmin, jmax, kmin, kmax));

      fAreaJ_.Insert(g1, g1, jmin, jmaxF, kmin, kmax,
                     fAreaJ_.Slice(i1, i1, jmin, jmaxF, kmin, kmax));

      fAreaK_.Insert(g1, g1, jmin, jmax, kmin, kmaxF,
                     fAreaK_.Slice(i1, i1, jmin, jmax, kmin, kmaxF));

      // assign face areas for second layer
      // one cell thick - use one cell for both ghost cells
      if (this->NumI() < 2) {
        fAreaI_.Insert(fg2, fg2, jmin, jmax, kmin, kmax,
                       fAreaI_.Slice(fi1, fi1, jmin, jmax, kmin, kmax));

        fAreaJ_.Insert(g2, g2, jmin, jmaxF, kmin, kmax,
                       fAreaJ_.Slice(i1, i1, jmin, jmaxF, kmin, kmax));

        fAreaK_.Insert(g2, g2, jmin, jmax, kmin, kmaxF,
                       fAreaK_.Slice(i1, i1, jmin, jmax, kmin, kmaxF));
      } else {
        fAreaI_.Insert(fg2, fg2, jmin, jmax, kmin, kmax,
                       fAreaI_.Slice(fi2, fi2, jmin, jmax, kmin, kmax));

        fAreaJ_.Insert(g2, g2, jmin, jmaxF, kmin, kmax,
                       fAreaJ_.Slice(i2, i2, jmin, jmaxF, kmin, kmax));

        fAreaK_.Insert(g2, g2, jmin, jmax, kmin, kmaxF,
                       fAreaK_.Slice(i2, i2, jmin, jmax, kmin, kmaxF));
      }

      // Assign cell centroid, and face centers

      // centroid is moved interior cell width in the boundary normal
      // direction
      multiArray3d<vector3d<double>> dist2Move =
          fCenterI_.Slice(bnd, bnd, jmin, jmax, kmin, kmax)
          - fCenterI_.Slice(fi1, fi1, jmin, jmax, kmin, kmax);
      multiArray3d<vector3d<double>> dist2MoveJ = dist2Move;
      multiArray3d<vector3d<double>> dist2MoveK = dist2Move;
      dist2MoveJ.GrowJ();
      dist2MoveK.GrowK();

      // first layer of ghost cells
      center_.Insert(g1, g1, jmin, jmax, kmin, kmax,
                     center_.Slice(i1, i1, jmin, jmax, kmin, kmax)
                     + dist2Move);

      // Assign face centers
      fCenterI_.Insert(fg1, fg1, jmin, jmax, kmin, kmax,
                       fCenterI_.Slice(bnd, bnd, jmin, jmax, kmin, kmax)
                       + dist2Move);

      fCenterJ_.Insert(g1, g1, jmin, jmaxF, kmin, kmax,
                       fCenterJ_.Slice(i1, i1, jmin, jmaxF, kmin, kmax)
                       + dist2MoveJ);

      fCenterK_.Insert(g1, g1, jmin, jmax, kmin, kmaxF,
                       fCenterK_.Slice(i1, i1, jmin, jmax, kmin, kmaxF)
                       + dist2MoveK);

      // second layer of ghost cells
      // one cell thick - use one cell for both ghost cells
      if (this->NumI() < 2) {
        dist2Move = dist2Move * 2.0;
        dist2MoveJ = dist2MoveJ * 2.0;
        dist2MoveK = dist2MoveK * 2.0;
      } else {
        dist2Move =
            fCenterI_.Slice(bnd, bnd, jmin, jmax, kmin, kmax)
            - fCenterI_.Slice(fi2, fi2, jmin, jmax, kmin, kmax);
        dist2MoveJ = dist2Move;
        dist2MoveK = dist2Move;
        dist2MoveJ.GrowJ();
        dist2MoveK.GrowK();
      }

      center_.Insert(g2, g2, jmin, jmax, kmin, kmax,
                     center_.Slice(i1, i1, jmin, jmax, kmin, kmax)
                     + dist2Move);

      fCenterI_.Insert(fg2, fg2, jmin, jmax, kmin, kmax,
                       fCenterI_.Slice(bnd, bnd, jmin, jmax, kmin, kmax)
                       + dist2Move);

      fCenterJ_.Insert(g2, g2, jmin, jmaxF, kmin, kmax,
                       fCenterJ_.Slice(i1, i1, jmin, jmaxF, kmin, kmax)
                       + dist2MoveJ);

      fCenterK_.Insert(g2, g2, jmin, jmax, kmin, kmaxF,
                       fCenterK_.Slice(i1, i1, jmin, jmax, kmin, kmaxF)
                       + dist2MoveK);

    //-----------------------------------------------------------------------
    // Assign ghost geometry for j-surfaces
    // -----------------------------------------------------------------------
    // only supply geometry values for non interblock BCs
    // for interblock do nothing
    } else if ((bc_.GetSurfaceType(ii) == 3 || bc_.GetSurfaceType(ii) == 4) &&
               bc_.GetBCTypes(ii) != "interblock") {
      // assign volume for first layer of ghost cells
      vol_.Insert(imin, imax, g1, g1, kmin, kmax,
                  vol_.Slice(imin, imax, i1, i1, kmin, kmax));

      // assign volume for second layer of ghost cells
      // one cell thick - use one cell for both ghost cells
      if (this->NumJ() < 2) {
        vol_.Insert(imin, imax, g2, g2, kmin, kmax,
                    vol_.Slice(imin, imax, i1, i1, kmin, kmax));
      } else {
        vol_.Insert(imin, imax, g2, g2, kmin, kmax,
                    vol_.Slice(imin, imax, i2, i2, kmin, kmax));
      }

      // assign face areas for first layer
      fAreaI_.Insert(imin, imaxF, g1, g1, kmin, kmax,
                     fAreaI_.Slice(imin, imaxF, i1, i1, kmin, kmax));

      fAreaJ_.Insert(imin, imax, fg1, fg1, kmin, kmax,
                     fAreaJ_.Slice(imin, imax, fi1, fi1, kmin, kmax));

      fAreaK_.Insert(imin, imax, g1, g1, kmin, kmaxF,
                     fAreaK_.Slice(imin, imax, i1, i1, kmin, kmaxF));

      // assign face areas for second layer
      // one cell thick - use one cell for both ghost cells
      if (this->NumJ() < 2) {
        fAreaI_.Insert(imin, imaxF, g2, g2, kmin, kmax,
                       fAreaI_.Slice(imin, imaxF, i1, i1, kmin, kmax));

        fAreaJ_.Insert(imin, imax, fg2, fg2, kmin, kmax,
                       fAreaJ_.Slice(imin, imax, fi1, fi1, kmin, kmax));

        fAreaK_.Insert(imin, imax, g2, g2, kmin, kmaxF,
                       fAreaK_.Slice(imin, imax, i1, i1, kmin, kmaxF));
      } else {
        fAreaI_.Insert(imin, imaxF, g2, g2, kmin, kmax,
                       fAreaI_.Slice(imin, imaxF, i2, i2, kmin, kmax));

        fAreaJ_.Insert(imin, imax, fg2, fg2, kmin, kmax,
                       fAreaJ_.Slice(imin, imax, fi2, fi2, kmin, kmax));

        fAreaK_.Insert(imin, imax, g2, g2, kmin, kmaxF,
                       fAreaK_.Slice(imin, imax, i2, i2, kmin, kmaxF));
      }

      // Assign cell centroid, and face centers

      // centroid is moved interior cell width in the boundary normal
      // direction
      multiArray3d<vector3d<double>> dist2Move =
          fCenterJ_.Slice(imin, imax, bnd, bnd, kmin, kmax)
          - fCenterJ_.Slice(imin, imax, fi1, fi1, kmin, kmax);
      multiArray3d<vector3d<double>> dist2MoveI = dist2Move;
      multiArray3d<vector3d<double>> dist2MoveK = dist2Move;
      dist2MoveI.GrowI();
      dist2MoveK.GrowK();

      // first layer of ghost cells
      center_.Insert(imin, imax, g1, g1, kmin, kmax,
                     center_.Slice(imin, imax, i1, i1, kmin, kmax)
                     + dist2Move);

      // Assign face centers
      fCenterI_.Insert(imin, imaxF, g1, g1, kmin, kmax,
                       fCenterI_.Slice(imin, imaxF, i1, i1, kmin, kmax)
                       + dist2MoveI);

      fCenterJ_.Insert(imin, imax, fg1, fg1, kmin, kmax,
                       fCenterJ_.Slice(imin, imax, bnd, bnd, kmin, kmax)
                       + dist2Move);

      fCenterK_.Insert(imin, imax, g1, g1, kmin, kmaxF,
                       fCenterK_.Slice(imin, imax, i1, i1, kmin, kmaxF)
                       + dist2MoveK);

      // second layer of ghost cells
      // one cell thick - use one cell for both ghost cells
      if (this->NumJ() < 2) {
        dist2Move = dist2Move * 2.0;
        dist2MoveI = dist2MoveI * 2.0;
        dist2MoveK = dist2MoveK * 2.0;
      } else {
        dist2Move =
            fCenterJ_.Slice(imin, imax, bnd, bnd, kmin, kmax)
            - fCenterJ_.Slice(imin, imax, fi2, fi2, kmin, kmax);
        dist2MoveI = dist2Move;
        dist2MoveK = dist2Move;
        dist2MoveI.GrowI();
        dist2MoveK.GrowK();
      }

      center_.Insert(imin, imax, g2, g2, kmin, kmax,
                     center_.Slice(imin, imax, i1, i1, kmin, kmax)
                     + dist2Move);

      fCenterI_.Insert(imin, imaxF, g2, g2, kmin, kmax,
                       fCenterI_.Slice(imin, imaxF, i1, i1, kmin, kmax)
                       + dist2MoveI);

      fCenterJ_.Insert(imin, imax, fg2, fg2, kmin, kmax,
                       fCenterJ_.Slice(imin, imax, bnd, bnd, kmin, kmax)
                       + dist2Move);

      fCenterK_.Insert(imin, imax, g2, g2, kmin, kmaxF,
                       fCenterK_.Slice(imin, imax, i1, i1, kmin, kmaxF)
                       + dist2MoveK);

    //-----------------------------------------------------------------------
    // Assign ghost geometry for k-surfaces
    // -----------------------------------------------------------------------
    // only supply geometry values for non interblock BCs
    // for interblock do nothing
    } else if ((bc_.GetSurfaceType(ii) == 5 || bc_.GetSurfaceType(ii) == 6) &&
               bc_.GetBCTypes(ii) != "interblock") {
      // assign volume for first layer of ghost cells
      vol_.Insert(imin, imax, jmin, jmax, g1, g1,
                  vol_.Slice(imin, imax, jmin, jmax, i1, i1));

      // assign volume for second layer of ghost cells
      // one cell thick - use one cell for both ghost cells
      if (this->NumK() < 2) {
        vol_.Insert(imin, imax, jmin, jmax, g2, g2,
                    vol_.Slice(imin, imax, jmin, jmax, i1, i1));
      } else {
        vol_.Insert(imin, imax, jmin, jmax, g2, g2,
                    vol_.Slice(imin, imax, jmin, jmax, i2, i2));
      }

      // assign face areas for first layer
      fAreaI_.Insert(imin, imaxF, jmin, jmax, g1, g1,
                     fAreaI_.Slice(imin, imaxF, jmin, jmax, i1, i1));

      fAreaJ_.Insert(imin, imax, jmin, jmaxF, g1, g1,
                     fAreaJ_.Slice(imin, imax, jmin, jmaxF, i1, i1));

      fAreaK_.Insert(imin, imax, jmin, jmax, fg1, fg1,
                     fAreaK_.Slice(imin, imax, jmin, jmax, fi1, fi1));

      // assign face areas for second layer
      // one cell thick - use one cell for both ghost cells
      if (this->NumK() < 2) {
        fAreaI_.Insert(imin, imaxF, jmin, jmax, g2, g2,
                       fAreaI_.Slice(imin, imaxF, jmin, jmax, i1, i1));

        fAreaJ_.Insert(imin, imax, jmin, jmaxF, g2, g2,
                       fAreaJ_.Slice(imin, imax, jmin, jmaxF, i1, i1));

        fAreaK_.Insert(imin, imax, jmin, jmax, fg2, fg2,
                       fAreaK_.Slice(imin, imax, jmin, jmax, fi1, fi1));
      } else {
        fAreaI_.Insert(imin, imaxF, jmin, jmax, g2, g2,
                       fAreaI_.Slice(imin, imaxF, jmin, jmax, i2, i2));

        fAreaJ_.Insert(imin, imax, jmin, jmaxF, g2, g2,
                       fAreaJ_.Slice(imin, imax, jmin, jmaxF, i2, i2));

        fAreaK_.Insert(imin, imax, jmin, jmax, fg2, fg2,
                       fAreaK_.Slice(imin, imax, jmin, jmax, fi2, fi2));
      }

      // Assign cell centroid, and face centers

      // centroid is moved interior cell width in the boundary normal
      // direction
      multiArray3d<vector3d<double>> dist2Move =
          fCenterK_.Slice(imin, imax, jmin, jmax, bnd, bnd)
          - fCenterK_.Slice(imin, imax, jmin, jmax, fi1, fi1);
      multiArray3d<vector3d<double>> dist2MoveI = dist2Move;
      multiArray3d<vector3d<double>> dist2MoveJ = dist2Move;
      dist2MoveI.GrowI();
      dist2MoveJ.GrowJ();

      // first layer of ghost cells
      center_.Insert(imin, imax, jmin, jmax, g1, g1,
                     center_.Slice(imin, imax, jmin, jmax, i1, i1)
                     + dist2Move);

      // Assign face centers
      fCenterI_.Insert(imin, imaxF, jmin, jmax, g1, g1,
                       fCenterI_.Slice(imin, imaxF, jmin, jmax, i1, i1)
                       + dist2MoveI);

      fCenterJ_.Insert(imin, imax, jmin, jmaxF, g1, g1,
                       fCenterJ_.Slice(imin, imax, jmin, jmaxF, i1, i1)
                       + dist2MoveJ);

      fCenterK_.Insert(imin, imax, jmin, jmax, fg1, fg1,
                       fCenterK_.Slice(imin, imax, jmin, jmax, bnd, bnd)
                       + dist2Move);

      // second layer of ghost cells
      // one cell thick - use one cell for both ghost cells
      if (this->NumK() < 2) {
        dist2Move = dist2Move * 2.0;
        dist2MoveI = dist2MoveI * 2.0;
        dist2MoveJ = dist2MoveJ * 2.0;
      } else {
        dist2Move =
            fCenterK_.Slice(imin, imax, jmin, jmax, bnd, bnd)
            - fCenterK_.Slice(imin, imax, jmin, jmax, fi2, fi2);
        dist2MoveI = dist2Move;
        dist2MoveJ = dist2Move;
        dist2MoveI.GrowI();
        dist2MoveJ.GrowJ();
      }

      center_.Insert(imin, imax, jmin, jmax, g2, g2,
                     center_.Slice(imin, imax, jmin, jmax, i1, i1)
                     + dist2Move);

      fCenterI_.Insert(imin, imaxF, jmin, jmax, g2, g2,
                       fCenterI_.Slice(imin, imaxF, jmin, jmax, i1, i1)
                       + dist2MoveI);

      fCenterJ_.Insert(imin, imax, jmin, jmaxF, g2, g2,
                       fCenterJ_.Slice(imin, imax, jmin, jmaxF, i1, i1)
                       + dist2MoveJ);

      fCenterK_.Insert(imin, imax, jmin, jmax, fg2, fg2,
                       fCenterK_.Slice(imin, imax, jmin, jmax, bnd, bnd)
                       + dist2Move);
    }
  }

  // fill ghost cell edge lines with geometric values
  // (*this).AssignGhostCellsGeomEdge();
}

/* Member function to assign geometric quantities such as volume, face area,
cell centroid, and face center to ghost cells located on the 12 block edges.
Assumes AssignGhostCellsGeom has already been run.

           ____ ____ ____ ____ ____ ____ ____ ____
          | G2 | G1 | X  | X  | X  | X  | X  | X  |
          |____|____|____|____|____|____|____|____|
         K| G2 | G1 | X  | X  | X  | X  | X  | X  |
         ^|____|____|____|____|____|____|____|____|
         || G2 | G1 | X  | X  | X  | X  | X  | X  |
         ||____|____|____|____|____|____|____|____|
         e| G2 | G1 | X* | X  | X  | X  | X  | X  |
          |____|____|____|____|____|____|____|____|
         1| E  | E  | G1 | G1 | G1 | G1 | G1 | G1 |
          |____|____|____|____|____|____|____|____|
         2| E  | E  | G2 | G2 | G2 | G2 | G2 | G2 |
          |____|____|____|____|____|____|____|____|
            2    1     e ----> J

In the above diagram the cells marked X represent physical cells. Cells marked
G1 and G2 represent the first and second layer of ghost cells respectively. The
cells marked E are the edge ghost cells that need to be assigned values. At each
corner location (X*) there are 4 edge ghost cells that need to be filled. The
axes on the side of the diagram indicate the coordinates of the edge ghost cells
(1, 2) as well as the coordinates of the adjacent regualar ghost cells (e).

The values at edge cell 1,1 are the average of the values at the two ghost cells
it touches at level "e". The values at edge cells 1,2 and 2,1 are identical to
the values of the ghost cells they tough at level "e". The values at edge cell
2,2 are the average of the values at the two (1,2 & 2,1) edge ghost cells it
touches.
*/
void procBlock::AssignGhostCellsGeomEdge() {
  // ------------------------------------------------------------------------
  // loop over 4 edges that run in i-direction
  for (int cc = 0; cc < 4; cc++) {
    // cc = 0 -> jl/kl
    // cc = 1 -> jl/ku
    // cc = 2 -> ju/kl
    // cc = 3 -> ju/ku

    // cell indices
    int imin = numGhosts_;
    int imax = this->NumI() + numGhosts_ - 1;

    int jp = (cc <= 1) ? numGhosts_ : this->NumJ() + numGhosts_ - 1;
    int jg1 = (cc <= 1) ? jp - 1 : jp + 1;
    int jg2 = (cc <= 1) ? jp - 2 : jp + 2;

    int kp = (cc % 2 == 0) ? numGhosts_ : this->NumK() + numGhosts_ - 1;
    int kg1 = (cc % 2 == 0) ? kp - 1 : kp + 1;
    int kg2 = (cc % 2 == 0) ? kp - 2 : kp + 2;

    // face indices
    // these only change from cell indices for upper edges
    // these should only be used for accessing faces in their corresponding
    // direction - i.e. jpF should only be used to access fAreaJ, or fCenterJ
    int imaxF = this->NumI() + numGhosts_;

    int jpF = (cc <= 1) ? numGhosts_ : this->NumJ() + numGhosts_;
    int jiF = (cc <= 1) ? jpF + 1 : jpF - 1;
    int jg1F = (cc <= 1) ? jpF - 1 : jpF + 1;
    int jg2F = (cc <= 1) ? jpF - 2 : jpF + 2;

    int kpF = (cc % 2 == 0) ? numGhosts_ : this->NumK() + numGhosts_;
    int kg1F = (cc % 2 == 0) ? kpF - 1 : kpF + 1;
    int kg2F = (cc % 2 == 0) ? kpF - 2 : kpF + 2;

    // Assign volumes
    vol_.Insert(imin, imax, jg1, jg1, kg1, kg1, 0.5 *
                (vol_.Slice(imin, imax, jp, jp, kg1, kg1) +
                 vol_.Slice(imin, imax, jg1, jg1, kp, kp)));
    vol_.Insert(imin, imax, jg1, jg1, kg2, kg2,
                vol_.Slice(imin, imax, jp, jp, kg2, kg2));
    vol_.Insert(imin, imax, jg2, jg2, kg1, kg1,
                vol_.Slice(imin, imax, jg2, jg2, kp, kp));
    vol_.Insert(imin, imax, jg2, jg2, kg2, kg2, 0.5 *
                (vol_.Slice(imin, imax, jg1, jg1, kg2, kg2) +
                 vol_.Slice(imin, imax, jg2, jg2, kg1, kg1)));

    // Assign face areas
    fAreaI_.Insert(imin, imaxF, jg1, jg1, kg1, kg1, 0.5 *
                   (fAreaI_.Slice(imin, imaxF, jp, jp, kg1, kg1) +
                    fAreaI_.Slice(imin, imaxF, jg1, jg1, kp, kp)));
    fAreaJ_.Insert(imin, imax, jg1F, jg1F, kg1, kg1, 0.5 *
                   (fAreaJ_.Slice(imin, imax, jpF, jpF, kg1, kg1) +
                    fAreaJ_.Slice(imin, imax, jg1F, jg1F, kp, kp)));
    fAreaK_.Insert(imin, imax, jg1, jg1, kg1F, kg1F, 0.5 *
                   (fAreaK_.Slice(imin, imax, jp, jp, kg1F, kg1F) +
                    fAreaK_.Slice(imin, imax, jg1, jg1, kpF, kpF)));

    fAreaI_.Insert(imin, imaxF, jg1, jg1, kg2, kg2,
                   fAreaI_.Slice(imin, imaxF, jp, jp, kg2, kg2));
    fAreaJ_.Insert(imin, imax, jg1F, jg1F, kg2, kg2,
                   fAreaJ_.Slice(imin, imax, jpF, jpF, kg2, kg2));
    fAreaK_.Insert(imin, imax, jg1, jg1, kg2F, kg2F,
                   fAreaK_.Slice(imin, imax, jp, jp, kg2F, kg2F));

    fAreaI_.Insert(imin, imaxF, jg2, jg2, kg1, kg1,
                   fAreaI_.Slice(imin, imaxF, jg2, jg2, kp, kp));
    fAreaJ_.Insert(imin, imax, jg2F, jg2F, kg1, kg1,
                   fAreaJ_.Slice(imin, imax, jg2F, jg2F, kp, kp));
    fAreaK_.Insert(imin, imax, jg2, jg2, kg1F, kg1F,
                   fAreaK_.Slice(imin, imax, jg2, jg2, kpF, kpF));

    fAreaI_.Insert(imin, imaxF, jg2, jg2, kg2, kg2, 0.5 *
                   (fAreaI_.Slice(imin, imaxF, jg1, jg1, kg2, kg2) +
                    fAreaI_.Slice(imin, imaxF, jg2, jg2, kg1, kg1)));
    fAreaJ_.Insert(imin, imax, jg2F, jg2F, kg2, kg2, 0.5 *
                   (fAreaJ_.Slice(imin, imax, jg1F, jg1F, kg2, kg2) +
                    fAreaJ_.Slice(imin, imax, jg2F, jg2F, kg1, kg1)));
    fAreaK_.Insert(imin, imax, jg2, jg2, kg2F, kg2F, 0.5 *
                   (fAreaK_.Slice(imin, imax, jg1, jg1, kg2F, kg2F) +
                    fAreaK_.Slice(imin, imax, jg2, jg2, kg1F, kg1F)));

    // Assign centroids
    multiArray3d<vector3d<double>> dist2MoveJ =
        fCenterJ_.Slice(imin, imax, jpF, jpF, kp, kp) -
        fCenterJ_.Slice(imin, imax, jiF, jiF, kp, kp);

    center_.Insert(imin, imax, jg1, jg1, kg1, kg1, dist2MoveJ +
                   center_.Slice(imin, imax, jp, jp, kg1, kg1));
    center_.Insert(imin, imax, jg1, jg1, kg2, kg2, dist2MoveJ +
                   center_.Slice(imin, imax, jp, jp, kg2, kg2));
    center_.Insert(imin, imax, jg2, jg2, kg1, kg1, dist2MoveJ +
                   center_.Slice(imin, imax, jg1, jg1, kg1, kg1));
    center_.Insert(imin, imax, jg2, jg2, kg2, kg2, dist2MoveJ +
                   center_.Slice(imin, imax, jg1, jg1, kg2, kg2));

    // Assign face centers
    multiArray3d<vector3d<double>> dist2MoveJf =
        fCenterJ_.Slice(imin, imaxF, jpF, jpF, kp, kp) -
        fCenterJ_.Slice(imin, imaxF, jiF, jiF, kp, kp);

    fCenterI_.Insert(imin, imaxF, jg1, jg1, kg1, kg1, dist2MoveJf +
                     fCenterI_.Slice(imin, imaxF, jp, jp, kg1, kg1));
    fCenterJ_.Insert(imin, imax, jg1F, jg1F, kg1, kg1, dist2MoveJ +
                     fCenterJ_.Slice(imin, imax, jpF, jpF, kg1, kg1));
    fCenterK_.Insert(imin, imax, jg1, jg1, kg1F, kg1F, dist2MoveJ +
                    fCenterK_.Slice(imin, imax, jp, jp, kg1F, kg1F));

    fCenterI_.Insert(imin, imaxF, jg1, jg1, kg2, kg2, dist2MoveJf +
                   fCenterI_.Slice(imin, imaxF, jp, jp, kg2, kg2));
    fCenterJ_.Insert(imin, imax, jg1F, jg1F, kg2, kg2, dist2MoveJ +
                   fCenterJ_.Slice(imin, imax, jpF, jpF, kg2, kg2));
    fCenterK_.Insert(imin, imax, jg1, jg1, kg2F, kg2F, dist2MoveJ +
                   fCenterK_.Slice(imin, imax, jp, jp, kg2F, kg2F));

    fCenterI_.Insert(imin, imaxF, jg2, jg2, kg1, kg1, dist2MoveJf +
                   fCenterI_.Slice(imin, imaxF, jg1, jg1, kg1, kg1));
    fCenterJ_.Insert(imin, imax, jg2F, jg2F, kg1, kg1, dist2MoveJ +
                   fCenterJ_.Slice(imin, imax, jg1F, jg1F, kg1, kg1));
    fCenterK_.Insert(imin, imax, jg2, jg2, kg1F, kg1F, dist2MoveJ +
                   fCenterK_.Slice(imin, imax, jg1, jg1, kg1F, kg1F));

    fCenterI_.Insert(imin, imaxF, jg2, jg2, kg2, kg2, dist2MoveJf +
                     fCenterI_.Slice(imin, imaxF, jg1, jg1, kg2, kg2));
    fCenterJ_.Insert(imin, imax, jg2F, jg2F, kg2, kg2, dist2MoveJ +
                     fCenterJ_.Slice(imin, imax, jg1F, jg1F, kg2, kg2));
    fCenterK_.Insert(imin, imax, jg2, jg2, kg2F, kg2F, dist2MoveJ +
                    fCenterK_.Slice(imin, imax, jg1, jg1, kg2F, kg2F));
  }

  // ------------------------------------------------------------------------
  // loop over 4 edges that run in j-direction
  for (int cc = 0; cc < 4; cc++) {
    // cc = 0 -> il/kl
    // cc = 1 -> il/ku
    // cc = 2 -> iu/kl
    // cc = 3 -> iu/ku

    // cell indices
    int jmin = numGhosts_;
    int jmax = this->NumJ() + numGhosts_ - 1;

    int ip = (cc <= 1) ? numGhosts_ : this->NumI() + numGhosts_ - 1;
    int ig1 = (cc <= 1) ? ip - 1 : ip + 1;
    int ig2 = (cc <= 1) ? ip - 2 : ip + 2;

    int kp = (cc % 2 == 0) ? numGhosts_ : this->NumK() + numGhosts_ - 1;
    int kg1 = (cc % 2 == 0) ? kp - 1 : kp + 1;
    int kg2 = (cc % 2 == 0) ? kp - 2 : kp + 2;

    // face indices
    // these only change from cell indices for upper edges
    // these should only be used for accessing faces in their corresponding
    // direction - i.e. ipF should only be used to access fAreaI, or fCenterI
    int jmaxF = this->NumJ() + numGhosts_;

    int ipF = (cc <= 1) ? numGhosts_ : this->NumI() + numGhosts_;
    int iiF = (cc <= 1) ? ipF + 1 : ipF - 1;
    int ig1F = (cc <= 1) ? ipF - 1 : ipF + 1;
    int ig2F = (cc <= 1) ? ipF - 2 : ipF + 2;

    int kpF = (cc % 2 == 0) ? numGhosts_ : this->NumK() + numGhosts_;
    int kg1F = (cc % 2 == 0) ? kpF - 1 : kpF + 1;
    int kg2F = (cc % 2 == 0) ? kpF - 2 : kpF + 2;

    // Assign volumes
    vol_.Insert(ig1, ig1, jmin, jmax, kg1, kg1, 0.5 *
                (vol_.Slice(ip, ip, jmin, jmax, kg1, kg1) +
                 vol_.Slice(ig1, ig1, jmin, jmax, kp, kp)));
    vol_.Insert(ig1, ig1, jmin, jmax, kg2, kg2,
                vol_.Slice(ip, ip, jmin, jmax, kg2, kg2));
    vol_.Insert(ig2, ig2, jmin, jmax, kg1, kg1,
                vol_.Slice(ig2, ig2, jmin, jmax, kp, kp));
    vol_.Insert(ig2, ig2, jmin, jmax, kg2, kg2, 0.5 *
                (vol_.Slice(ig1, ig1, jmin, jmax, kg2, kg2) +
                 vol_.Slice(ig2, ig2, jmin, jmax, kg1, kg1)));

    // Assign face areas
    fAreaI_.Insert(ig1F, ig1F, jmin, jmax, kg1, kg1, 0.5 *
                   (fAreaI_.Slice(ipF, ipF, jmin, jmax, kg1, kg1) +
                    fAreaI_.Slice(ig1F, ig1F, jmin, jmax, kp, kp)));
    fAreaJ_.Insert(ig1, ig1, jmin, jmaxF, kg1, kg1, 0.5 *
                   (fAreaJ_.Slice(ip, ip, jmin, jmaxF, kg1, kg1) +
                    fAreaJ_.Slice(ig1, ig1, jmin, jmaxF, kp, kp)));
    fAreaK_.Insert(ig1, ig1, jmin, jmax, kg1F, kg1F, 0.5 *
                   (fAreaK_.Slice(ip, ip, jmin, jmax, kg1F, kg1F) +
                    fAreaK_.Slice(ig1, ig1, jmin, jmax, kpF, kpF)));

    fAreaI_.Insert(ig1F, ig1F, jmin, jmax, kg2, kg2,
                   fAreaI_.Slice(ipF, ipF, jmin, jmax, kg2, kg2));
    fAreaJ_.Insert(ig1, ig1, jmin, jmaxF, kg2, kg2,
                   fAreaJ_.Slice(ip, ip, jmin, jmaxF, kg2, kg2));
    fAreaK_.Insert(ig1, ig1, jmin, jmax, kg2F, kg2F,
                   fAreaK_.Slice(ip, ip, jmin, jmax, kg2F, kg2F));

    fAreaI_.Insert(ig2F, ig2F, jmin, jmax, kg1, kg1,
                   fAreaI_.Slice(ig2F, ig2F, jmin, jmax, kp, kp));
    fAreaJ_.Insert(ig2, ig2, jmin, jmaxF, kg1, kg1,
                   fAreaJ_.Slice(ig2, ig2, jmin, jmaxF, kp, kp));
    fAreaK_.Insert(ig2, ig2, jmin, jmax, kg1F, kg1F,
                   fAreaK_.Slice(ig2, ig2, jmin, jmax, kpF, kpF));

    fAreaI_.Insert(ig2F, ig2F, jmin, jmax, kg2, kg2, 0.5 *
                   (fAreaI_.Slice(ig1F, ig1F, jmin, jmax, kg2, kg2) +
                    fAreaI_.Slice(ig2F, ig2F, jmin, jmax, kg1, kg1)));
    fAreaJ_.Insert(ig2, ig2, jmin, jmaxF, kg2, kg2, 0.5 *
                   (fAreaJ_.Slice(ig1, ig1, jmin, jmaxF, kg2, kg2) +
                    fAreaJ_.Slice(ig2, ig2, jmin, jmaxF, kg1, kg1)));
    fAreaK_.Insert(ig2, ig2, jmin, jmax, kg2F, kg2F, 0.5 *
                   (fAreaK_.Slice(ig1, ig1, jmin, jmax, kg2F, kg2F) +
                    fAreaK_.Slice(ig2, ig2, jmin, jmax, kg1F, kg1F)));

    // Assign centroids
    multiArray3d<vector3d<double>> dist2MoveI =
        fCenterI_.Slice(ipF, ipF, jmin, jmax, kp, kp) -
        fCenterI_.Slice(iiF, iiF, jmin, jmax, kp, kp);

    center_.Insert(ig1, ig1, jmin, jmax, kg1, kg1, dist2MoveI +
                   center_.Slice(ip, ip, jmin, jmax, kg1, kg1));
    center_.Insert(ig1, ig1, jmin, jmax, kg2, kg2, dist2MoveI +
                   center_.Slice(ip, ip, jmin, jmax, kg2, kg2));
    center_.Insert(ig2, ig2, jmin, jmax, kg1, kg1, dist2MoveI +
                   center_.Slice(ig1, ig1, jmin, jmax, kg1, kg1));
    center_.Insert(ig2, ig2, jmin, jmax, kg2, kg2, dist2MoveI +
                   center_.Slice(ig1, ig1, jmin, jmax, kg2, kg2));

    // Assign face centers
    multiArray3d<vector3d<double>> dist2MoveIf =
        fCenterI_.Slice(ipF, ipF, jmin, jmaxF, kp, kp) -
        fCenterI_.Slice(iiF, iiF, jmin, jmaxF, kp, kp);

    fCenterI_.Insert(ig1F, ig1F, jmin, jmax, kg1, kg1, dist2MoveI +
                     fCenterI_.Slice(ipF, ipF, jmin, jmax, kg1, kg1));
    fCenterJ_.Insert(ig1, ig1, jmin, jmaxF, kg1, kg1, dist2MoveIf +
                     fCenterJ_.Slice(ip, ip, jmin, jmaxF, kg1, kg1));
    fCenterK_.Insert(ig1, ig1, jmin, jmax, kg1F, kg1F, dist2MoveI +
                    fCenterK_.Slice(ip, ip, jmin, jmax, kg1F, kg1F));

    fCenterI_.Insert(ig1F, ig1F, jmin, jmax, kg2, kg2, dist2MoveI +
                   fCenterI_.Slice(ipF, ipF, jmin, jmax, kg2, kg2));
    fCenterJ_.Insert(ig1, ig1, jmin, jmaxF, kg2, kg2, dist2MoveIf +
                   fCenterJ_.Slice(ip, ip, jmin, jmaxF, kg2, kg2));
    fCenterK_.Insert(ig1, ig1, jmin, jmax, kg2F, kg2F, dist2MoveI +
                   fCenterK_.Slice(ip, ip, jmin, jmax, kg2F, kg2F));

    fCenterI_.Insert(ig2F, ig2F, jmin, jmax, kg1, kg1, dist2MoveI +
                   fCenterI_.Slice(ig1F, ig1F, jmin, jmax, kg1, kg1));
    fCenterJ_.Insert(ig2, ig2, jmin, jmaxF, kg1, kg1, dist2MoveIf +
                   fCenterJ_.Slice(ig1, ig1, jmin, jmaxF, kg1, kg1));
    fCenterK_.Insert(ig2, ig2, jmin, jmax, kg1F, kg1F, dist2MoveI +
                   fCenterK_.Slice(ig1, ig1, jmin, jmax, kg1F, kg1F));

    fCenterI_.Insert(ig2F, ig2F, jmin, jmax, kg2, kg2, dist2MoveI +
                     fCenterI_.Slice(ig1F, ig1F, jmin, jmaxF, kg2, kg2));
    fCenterJ_.Insert(ig2, ig2, jmin, jmaxF, kg2, kg2, dist2MoveIf +
                     fCenterJ_.Slice(ig1, ig1, jmin, jmaxF, kg2, kg2));
    fCenterK_.Insert(ig2, ig2, jmin, jmax, kg2F, kg2F, dist2MoveI +
                    fCenterK_.Slice(ig1, ig1, jmin, jmax, kg2F, kg2F));
  }

  // ------------------------------------------------------------------------
  // loop over 4 edges that run in k-direction
  for (int cc = 0; cc < 4; cc++) {
    // cc = 0 -> il/jl
    // cc = 1 -> il/ju
    // cc = 2 -> iu/jl
    // cc = 3 -> iu/ju

    // cell indices
    int kmin = numGhosts_;
    int kmax = this->NumK() + numGhosts_ - 1;

    int ip = (cc <= 1) ? numGhosts_ : this->NumI() + numGhosts_ - 1;
    int ig1 = (cc <= 1) ? ip - 1 : ip + 1;
    int ig2 = (cc <= 1) ? ip - 2 : ip + 2;

    int jp = (cc % 2 == 0) ? numGhosts_ : this->NumJ() + numGhosts_ - 1;
    int jg1 = (cc % 2 == 0) ? jp - 1 : jp + 1;
    int jg2 = (cc % 2 == 0) ? jp - 2 : jp + 2;

    // face indices
    // these only change from cell indices for upper edges
    // these should only be used for accessing faces in their corresponding
    // direction - i.e. ipF should only be used to access fAreaI, or fCenterI
    int kmaxF = this->NumK() + numGhosts_;

    int ipF = (cc <= 1) ? numGhosts_ : this->NumI() + numGhosts_;
    int iiF = (cc <= 1) ? ipF + 1 : ipF - 1;
    int ig1F = (cc <= 1) ? ipF - 1 : ipF + 1;
    int ig2F = (cc <= 1) ? ipF - 2 : ipF + 2;

    int jpF = (cc % 2 == 0) ? numGhosts_ : this->NumJ() + numGhosts_;
    int jg1F = (cc % 2 == 0) ? jpF - 1 : jpF + 1;
    int jg2F = (cc % 2 == 0) ? jpF - 2 : jpF + 2;

    // Assign volumes
    vol_.Insert(ig1, ig1, jg1, jg1, kmin, kmax, 0.5 *
                (vol_.Slice(ip, ip, jg1, jg1, kmin, kmax) +
                 vol_.Slice(ig1, ig1, jp, jp, kmin, kmax)));
    vol_.Insert(ig1, ig1, jg2, jg2, kmin, kmax,
                vol_.Slice(ip, ip, jg2, jg2, kmin, kmax));
    vol_.Insert(ig2, ig2, jg1, jg1, kmin, kmax,
                vol_.Slice(ig2, ig2, jp, jp, kmin, kmax));
    vol_.Insert(ig2, ig2, jg2, jg2, kmin, kmax, 0.5 *
                (vol_.Slice(ig1, ig1, jg2, jg2, kmin, kmax) +
                 vol_.Slice(ig2, ig2, jg1, jg1, kmin, kmax)));

    // Assign face areas
    fAreaI_.Insert(ig1F, ig1F, jg1, jg1, kmin, kmax, 0.5 *
                   (fAreaI_.Slice(ipF, ipF, jg1, jg1, kmin, kmax) +
                    fAreaI_.Slice(ig1F, ig1F, jp, jp, kmin, kmax)));
    fAreaJ_.Insert(ig1, ig1, jg1F, jg1F, kmin, kmax, 0.5 *
                   (fAreaJ_.Slice(ip, ip, jg1F, jg1F, kmin, kmax) +
                    fAreaJ_.Slice(ig1, ig1, jpF, jpF, kmin, kmax)));
    fAreaK_.Insert(ig1, ig1, jg1, jg1, kmin, kmaxF, 0.5 *
                   (fAreaK_.Slice(ip, ip, jg1, jg1, kmin, kmaxF) +
                    fAreaK_.Slice(ig1, ig1, jp, jp, kmin, kmaxF)));

    fAreaI_.Insert(ig1F, ig1F, jg2, jg2, kmin, kmax,
                   fAreaI_.Slice(ipF, ipF, jg2, jg2, kmin, kmax));
    fAreaJ_.Insert(ig1, ig1, jg2F, jg2F, kmin, kmax,
                   fAreaJ_.Slice(ip, ip, jg2F, jg2F, kmin, kmax));
    fAreaK_.Insert(ig1, ig1, jg2F, jg2F, kmin, kmaxF,
                   fAreaK_.Slice(ip, ip, jg2F, jg2F, kmin, kmaxF));

    fAreaI_.Insert(ig2F, ig2F, jg1, jg1, kmin, kmax,
                   fAreaI_.Slice(ig2F, ig2F, jp, jp, kmin, kmax));
    fAreaJ_.Insert(ig2, ig2, jg1F, jg1F, kmin, kmax,
                   fAreaJ_.Slice(ig2, ig2, jpF, jpF, kmin, kmax));
    fAreaK_.Insert(ig2, ig2, jg1, jg1, kmin, kmaxF,
                   fAreaK_.Slice(ig2, ig2, jp, jp, kmin, kmaxF));

    fAreaI_.Insert(ig2F, ig2F, jg2, jg2, kmin, kmax, 0.5 *
                   (fAreaI_.Slice(ig1F, ig1F, jg2, jg2, kmin, kmax) +
                    fAreaI_.Slice(ig2F, ig2F, jg1, jg1, kmin, kmax)));
    fAreaJ_.Insert(ig2, ig2, jg2F, jg2F, kmin, kmax, 0.5 *
                   (fAreaJ_.Slice(ig1, ig1, jg2F, jg2F, kmin, kmax) +
                    fAreaJ_.Slice(ig2, ig2, jg1F, jg1F, kmin, kmax)));
    fAreaK_.Insert(ig2, ig2, jg2, jg2, kmin, kmaxF, 0.5 *
                   (fAreaK_.Slice(ig1, ig1, jg2, jg2, kmin, kmaxF) +
                    fAreaK_.Slice(ig2, ig2, jg1, jg1, kmin, kmaxF)));

    // Assign centroids
    multiArray3d<vector3d<double>> dist2MoveI =
        fCenterI_.Slice(ipF, ipF, jp, jp, kmin, kmax) -
        fCenterI_.Slice(iiF, iiF, jp, jp, kmin, kmax);

    center_.Insert(ig1, ig1, jg1, jg1, kmin, kmax, dist2MoveI +
                   center_.Slice(ip, ip, jg1, jg1, kmin, kmax));
    center_.Insert(ig1, ig1, jg2, jg2, kmin, kmax, dist2MoveI +
                   center_.Slice(ip, ip, jg2, jg2, kmin, kmax));
    center_.Insert(ig2, ig2, jg1, jg1, kmin, kmax, dist2MoveI +
                   center_.Slice(ig1, ig1, jg1, jg1, kmin, kmax));
    center_.Insert(ig2, ig2, jg2, jg2, kmin, kmax, dist2MoveI +
                   center_.Slice(ig1, ig1, jg2, jg2, kmin, kmax));

    // Assign face centers
    multiArray3d<vector3d<double>> dist2MoveIf =
        fCenterI_.Slice(ipF, ipF, jp, jp, kmin, kmaxF) -
        fCenterI_.Slice(iiF, iiF, jp, jp, kmin, kmaxF);

    fCenterI_.Insert(ig1F, ig1F, jg1, jg1, kmin, kmax, dist2MoveI +
                     fCenterI_.Slice(ipF, ipF, jg1, jg1, kmin, kmax));
    fCenterJ_.Insert(ig1, ig1, jg1F, jg1F, kmin, kmax, dist2MoveI +
                     fCenterJ_.Slice(ip, ip, jg1F, jg1F, kmin, kmax));
    fCenterK_.Insert(ig1, ig1, jg1, jg1, kmin, kmaxF, dist2MoveIf +
                    fCenterK_.Slice(ip, ip, jg1, jg1, kmin, kmaxF));

    fCenterI_.Insert(ig1F, ig1F, jg2, jg2, kmin, kmax, dist2MoveI +
                   fCenterI_.Slice(ipF, ipF, jg2, jg2, kmin, kmax));
    fCenterJ_.Insert(ig1, ig1, jg2F, jg2F, kmin, kmax, dist2MoveI +
                   fCenterJ_.Slice(ip, ip, jg2F, jg2F, kmin, kmax));
    fCenterK_.Insert(ig1, ig1, jg2, jg2, kmin, kmaxF, dist2MoveIf +
                   fCenterK_.Slice(ip, ip, jg2, jg2, kmin, kmaxF));

    fCenterI_.Insert(ig2F, ig2F, jg1, jg1, kmin, kmax, dist2MoveI +
                   fCenterI_.Slice(ig1F, ig1F, jg1, jg1, kmin, kmax));
    fCenterJ_.Insert(ig2, ig2, jg1F, jg1F, kmin, kmax, dist2MoveI +
                   fCenterJ_.Slice(ig1, ig1, jg1F, jg1F, kmin, kmax));
    fCenterK_.Insert(ig2, ig2, jg1, jg1, kmin, kmaxF, dist2MoveIf +
                   fCenterK_.Slice(ig1, ig1, jg1, jg1, kmin, kmaxF));

    fCenterI_.Insert(ig2F, ig2F, jg2, jg2, kmin, kmax, dist2MoveI +
                     fCenterI_.Slice(ig1F, ig1F, jg2, jg2, kmin, kmax));
    fCenterJ_.Insert(ig2, ig2, jg2F, jg2F, kmin, kmax, dist2MoveI +
                     fCenterJ_.Slice(ig1, ig1, jg2F, jg2F, kmin, kmax));
    fCenterK_.Insert(ig2, ig2, jg2, jg2, kmin, kmaxF, dist2MoveIf +
                    fCenterK_.Slice(ig1, ig1, jg2, jg2, kmin, kmaxF));
  }
}

/* Member function to assign values for ghost cells for the inviscid flux
calculation. This function assigns values for regular ghost cells and "edge"
ghost cells. "Corner" cells are left with no value as they are not used.
           ____ ____ ____ ____ ____ ____ ____ ____
          | E  | E  | G2 | G2 | G2 | G2 | E  | E  |
          |____|____|____|____|____|____|____|____|
          | E  | E  | G1 | G1 | G1 | G1 | E  | E  |
          |____|____|____|____|____|____|____|____|
          | G2 | G1 | X  | X  | X  | X  | G1 | G2 |
          |____|____|____|____|____|____|____|____|
          | G2 | G1 | X  | X  | X  | X  | G1 | G2 |
          |____|____|____|____|____|____|____|____|
          | E  | E  | G1 | G1 | G1 | G1 | E  | E  |
          |____|____|____|____|____|____|____|____|
          | E  | E  | G2 | G2 | G2 | G2 | E  | E  |
          |____|____|____|____|____|____|____|____|

In the above diagram where X represents the physical cells, cells marked G
(regular ghost cells) and E ("edge" ghost cells) are assigned geometric
values. G1 represents the first layer of ghost cells and G2 represents the
second layer.
*/
void procBlock::AssignInviscidGhostCells(const input &inp,
                                         const idealGas &eos,
                                         const sutherland &suth) {
  // inp -- all input variables
  // eos -- equation of state
  // suth -- sutherland's law for viscosity

  // loop over all boundary surfaces
  for (int ii = 0; ii < bc_.NumSurfaces(); ii++) {
    // Get surface boundaries, and adjust them for ghost cells
    int imin = bc_.GetIMin(ii) - 1 + numGhosts_;
    int imax = bc_.GetIMax(ii) - 2 + numGhosts_;
    int jmin = bc_.GetJMin(ii) - 1 + numGhosts_;
    int jmax = bc_.GetJMax(ii) - 2 + numGhosts_;
    int kmin = bc_.GetKMin(ii) - 1 + numGhosts_;
    int kmax = bc_.GetKMax(ii) - 2 + numGhosts_;

    int g1, g2, i1, i2;  // indices for cells
    int bnd;  // indices for faces
    if (bc_.GetSurfaceType(ii) == 2) {  // upper i-surface
      g2 = imax + 2;
      g1 = imax + 1;
      i1 = imax;
      i2 = imax - 1;

      bnd = imax + 1;
    } else if (bc_.GetSurfaceType(ii) == 4) {  // upper j-surface
      g2 = jmax + 2;
      g1 = jmax + 1;
      i1 = jmax;
      i2 = jmax - 1;

      bnd = jmax + 1;
    } else if (bc_.GetSurfaceType(ii) == 6) {  // upper k-surface
      g2 = kmax + 2;
      g1 = kmax + 1;
      i1 = kmax;
      i2 = kmax - 1;

      bnd = kmax + 1;
    } else {  // lower surface
      g2 = 0;
      g1 = 1;
      i1 = 2;
      i2 = 3;

      bnd = 2;
    }

    //-----------------------------------------------------------------------
    // Assign ghost cells for i-surfaces
    // -----------------------------------------------------------------------
    // only supply cell values for non interblock BCs
    // for interblock do nothing
    if ((bc_.GetSurfaceType(ii) == 1 || bc_.GetSurfaceType(ii) == 2) &&
        bc_.GetBCTypes(ii) != "interblock") {
      string surf = (bc_.GetSurfaceType(ii) == 1) ? "il" : "iu";
      string bcName = (bc_.GetBCTypes(ii) == "viscousWall") ? "slipWall" :
          bc_.GetBCTypes(ii);
      // assign state for first layer of ghost cells
      multiArray3d<unitVec3dMag<double>> faceAreas =
          fAreaI_.Slice(bnd, bnd, jmin, jmax, kmin, kmax);
      multiArray3d<primVars> boundaryStates =
          state_.Slice(i1, i1, jmin, jmax, kmin, kmax);
      multiArray3d<primVars> ghostStates =
          GetGhostStates(boundaryStates, bcName, faceAreas, surf, inp, eos,
                         suth, 1);

      state_.Insert(g1, g1, jmin, jmax, kmin, kmax, ghostStates);

      // assign state for second layer of ghost cells
      // one cell thick - use one cell for both ghost cells
      if (this->NumI() < 2) {
        state_.Insert(g2, g2, jmin, jmax, kmin, kmax, ghostStates);
      } else {
        // if slipWall reflect 2nd interior state instead of extrapolation
        if (bcName == "slipWall") {
          boundaryStates = state_.Slice(i2, i2, jmin, jmax, kmin, kmax);
        }
        ghostStates = GetGhostStates(boundaryStates, bcName, faceAreas, surf,
                                     inp, eos, suth, 2);

        state_.Insert(g2, g2, jmin, jmax, kmin, kmax, ghostStates);
      }

    //-----------------------------------------------------------------------
    // Assign ghost cells for j-surfaces
    // -----------------------------------------------------------------------
    // only supply cell values for non interblock BCs
    // for interblock do nothing
    } else if ((bc_.GetSurfaceType(ii) == 3 || bc_.GetSurfaceType(ii) == 4) &&
               bc_.GetBCTypes(ii) != "interblock") {
      string surf = (bc_.GetSurfaceType(ii) == 3) ? "jl" : "ju";
      string bcName = (bc_.GetBCTypes(ii) == "viscousWall") ? "slipWall" :
          bc_.GetBCTypes(ii);
      // assign state for first layer of ghost cells
      multiArray3d<unitVec3dMag<double>> faceAreas =
          fAreaJ_.Slice(imin, imax, bnd, bnd, kmin, kmax);
      multiArray3d<primVars> boundaryStates =
          state_.Slice(imin, imax, i1, i1, kmin, kmax);
      multiArray3d<primVars> ghostStates =
          GetGhostStates(boundaryStates, bcName, faceAreas, surf, inp, eos,
                         suth, 1);

      // assign state for first layer of ghost cells
      state_.Insert(imin, imax, g1, g1, kmin, kmax, ghostStates);

      // assign state for second layer of ghost cells
      // one cell thick - use one cell for both ghost cells
      if (this->NumJ() < 2) {
        state_.Insert(imin, imax, g2, g2, kmin, kmax, ghostStates);
      } else {
        // if slipWall reflect 2nd interior state instead of extrapolation
        if (bcName == "slipWall") {
          boundaryStates = state_.Slice(imin, imax, i2, i2, kmin, kmax);
        }
        ghostStates = GetGhostStates(boundaryStates, bcName, faceAreas, surf,
                                     inp, eos, suth, 2);

        state_.Insert(imin, imax, g2, g2, kmin, kmax, ghostStates);
      }

    //-----------------------------------------------------------------------
    // Assign ghost cells for k-surfaces
    // -----------------------------------------------------------------------
    // only supply cell values for non interblock BCs
    // for interblock do nothing
    } else if ((bc_.GetSurfaceType(ii) == 5 || bc_.GetSurfaceType(ii) == 6) &&
               bc_.GetBCTypes(ii) != "interblock") {
      string surf = (bc_.GetSurfaceType(ii) == 5) ? "kl" : "ku";
      string bcName = (bc_.GetBCTypes(ii) == "viscousWall") ? "slipWall" :
          bc_.GetBCTypes(ii);
      // assign state for first layer of ghost cells
      multiArray3d<unitVec3dMag<double>> faceAreas =
          fAreaK_.Slice(imin, imax, jmin, jmax, bnd, bnd);
      multiArray3d<primVars> boundaryStates =
          state_.Slice(imin, imax, jmin, jmax, i1, i1);
      multiArray3d<primVars> ghostStates =
          GetGhostStates(boundaryStates, bcName, faceAreas, surf, inp, eos,
                         suth, 1);

      // assign state for first layer of ghost cells
      state_.Insert(imin, imax, jmin, jmax, g1, g1, ghostStates);

      // assign state for second layer of ghost cells
      // one cell thick - use one cell for both ghost cells
      if (this->NumK() < 2) {
        state_.Insert(imin, imax, jmin, jmax, g2, g2, ghostStates);
      } else {
        // if slipWall reflect 2nd interior state instead of extrapolation
        if (bcName == "slipWall") {
          boundaryStates = state_.Slice(imin, imax, jmin, jmax, i2, i2);
        }
        ghostStates = GetGhostStates(boundaryStates, bcName, faceAreas, surf,
                                     inp, eos, suth, 2);

        state_.Insert(imin, imax, jmin, jmax, g2, g2, ghostStates);
      }
    }
  }

  // assign values to edge ghost cells
  // (*this).AssignInviscidGhostCellsEdge(inp, eos, suth);
}

/* Member function to assign values to ghost cells located on the 12 block edges
for the inviscid flux calculation. Assumes AssignInviscidGhostCells has already
been run.
           ____ ____ ____ ____ ____ ____ ____ ____
          | G2 | G1 | X  | X  | X  | X  | X  | X  |
          |____|____|____|____|____|____|____|____|
         K| G2 | G1 | X  | X  | X  | X  | X  | X  |
         ^|____|____|____|____|____|____|____|____|
         || G2 | G1 | X  | X  | X  | X  | X  | X  |
         ||____|____|____|____|____|____|____|____|
         e| G2 | G1 | X* | X  | X  | X  | X  | X  |
          |____|____|____|____|____|____|____|____|
         1| E  | E  | G1 | G1 | G1 | G1 | G1 | G1 |
          |____|____|____|____|____|____|____|____|
         2| E  | E  | G2 | G2 | G2 | G2 | G2 | G2 |
          |____|____|____|____|____|____|____|____|
            2    1     e ----> J

In the above diagram the cells marked X represent physical cells. Cells marked
G1 and G2 represent the first and second layer of ghost cells respectively. The
cells marked E are the edge ghost cells that need to be assigned values. At each
corner location (X*) there are 4 edge ghost cells that need to be filled. The
axes on the side of the diagram indicate the coordinates of the edge ghost cells
(1, 2) as well as the coordinates of the adjacent regualar ghost cells (e).

The values at edge cell 1,1 are the average of the values at the two ghost cells
it touches at level "e". The values at edge cells 1,2 and 2,1 are identical to
the values of the ghost cells they tough at level "e". The values at edge cell
2,2 are the average of the values at the two (1,2 & 2,1) edge ghost cells it
touches. The exception to this rule occurs when either of the boundaries that meet
at the corner are wall boundaries (slipWall, viscousWall) and the other is not.
When this occurs the wall boundaries are "extended" into the ghost cells. This
implementation is described in Blazek.
*/
void procBlock::AssignInviscidGhostCellsEdge(const input &inp,
                                             const idealGas &eos,
                                             const sutherland &suth) {
  // inp -- all input variables
  // eos -- equation of state
  // suth -- sutherland's law for viscosity

  // ------------------------------------------------------------------------
  // loop over 4 edges that run in i-direction
  for (int cc = 0; cc < 4; cc++) {
    // cc = 0 -> jl/kl
    // cc = 1 -> jl/ku
    // cc = 2 -> ju/kl
    // cc = 3 -> ju/ku

    // cell indices
    int imin = numGhosts_;
    int imax = this->NumI() + numGhosts_ - 1;

    int jp = (cc <= 1) ? numGhosts_ : this->NumJ() + numGhosts_ - 1;
    int ji1 = (cc <= 1) ? jp + 1 : jp - 1;
    int jg1 = (cc <= 1) ? jp - 1 : jp + 1;
    int jg2 = (cc <= 1) ? jp - 2 : jp + 2;

    int kp = (cc % 2 == 0) ? numGhosts_ : this->NumK() + numGhosts_ - 1;
    int ki1 = (cc % 2 == 0) ? kp + 1 : kp - 1;
    int kg1 = (cc % 2 == 0) ? kp - 1 : kp + 1;
    int kg2 = (cc % 2 == 0) ? kp - 2 : kp + 2;

    // surface types of surfaces forming edge
    string surfJ = (cc <= 1) ? "jl" : "ju";
    string surfK = (cc % 2 == 0) ? "kl" : "ku";

    // face indices
    // these only change from cell indices for upper edges
    // these should only be used for accessing faces in their corresponding
    // direction - i.e. jpF should only be used to access fAreaJ, or fCenterJ
    int jpF = (cc <= 1) ? numGhosts_ : this->NumJ() + numGhosts_;
    int kpF = (cc % 2 == 0) ? numGhosts_ : this->NumK() + numGhosts_;

    for (int ii = imin; ii <= imax; ii++) {
      // boundary conditions at corner
      string bc_J = bc_.GetBCName(ii - numGhosts_, jpF - numGhosts_,
                                  kp - numGhosts_, surfJ);
      bc_J = (bc_J == "viscousWall") ? "slipWall" : bc_J;
      string bc_K = bc_.GetBCName(ii - numGhosts_, jp - numGhosts_,
                                  kpF - numGhosts_, surfK);
      bc_K = (bc_K == "viscousWall") ? "slipWall" : bc_K;

      // Assign states
      // j surface is a wall, but k surface is not - extend wall bc
      if (bc_J == "slipWall" && bc_K != "slipWall") {
        state_(ii, jg1, kg1) = state_(ii, jp, kg1).GetGhostState(
            bc_J, this->FAreaUnitJ(ii, jpF, kg1), surfJ, inp, eos, suth, 1);
        state_(ii, jg1, kg2) = state_(ii, jp, kg2).GetGhostState(
            bc_J, this->FAreaUnitJ(ii, jpF, kg2), surfJ, inp, eos, suth, 1);
        state_(ii, jg2, kg1) = state_(ii, ji1, kg1).GetGhostState(
            bc_J, this->FAreaUnitJ(ii, jpF, kg1), surfJ, inp, eos, suth, 2);
        state_(ii, jg2, kg2) = state_(ii, ji1, kg2).GetGhostState(
            bc_J, this->FAreaUnitJ(ii, jpF, kg2), surfJ, inp, eos, suth, 2);
        // k surface is a wall, but j surface is not - extend wall bc
      } else if (bc_J != "slipWall" && bc_K == "slipWall") {
        state_(ii, jg1, kg1) = state_(ii, jg1, kp).GetGhostState(
            bc_K, this->FAreaUnitK(ii, jg1, kpF), surfK, inp, eos, suth, 1);
        state_(ii, jg2, kg1) = state_(ii, jg2, kp).GetGhostState(
            bc_K, this->FAreaUnitK(ii, jg2, kpF), surfK, inp, eos, suth, 1);
        state_(ii, jg1, kg2) = state_(ii, jg1, ki1).GetGhostState(
            bc_K, this->FAreaUnitK(ii, jg1, kpF), surfK, inp, eos, suth, 2);
        state_(ii, jg2, kg2) = state_(ii, jg2, ki1).GetGhostState(
            bc_K, this->FAreaUnitK(ii, jg2, kpF), surfK, inp, eos, suth, 2);
      } else {  // both surfaces or neither are walls - proceed as normal
        state_(ii, jg1, kg1) = 0.5 * (state_(ii, jp, kg1) +
                                      state_(ii, jg1, kp));
        state_(ii, jg1, kg2) = state_(ii, jp, kg2);
        state_(ii, jg2, kg1) = state_(ii, jg2, kp);
        state_(ii, jg2, kg2) = 0.5 * (state_(ii, jg1, kg2) +
                                      state_(ii, jg2, kg1));
      }
    }
  }

  // ------------------------------------------------------------------------
  // loop over 4 edges that run in j-direction
  for (int cc = 0; cc < 4; cc++) {
    // cc = 0 -> il/kl
    // cc = 1 -> il/ku
    // cc = 2 -> iu/kl
    // cc = 3 -> iu/ku

    // cell indices
    int jmin = numGhosts_;
    int jmax = this->NumJ() + numGhosts_ - 1;

    int ip = (cc <= 1) ? numGhosts_ : this->NumI() + numGhosts_ - 1;
    int ii1 = (cc <= 1) ? ip + 1 : ip - 1;
    int ig1 = (cc <= 1) ? ip - 1 : ip + 1;
    int ig2 = (cc <= 1) ? ip - 2 : ip + 2;

    int kp = (cc % 2 == 0) ? numGhosts_ : this->NumK() + numGhosts_ - 1;
    int ki1 = (cc % 2 == 0) ? kp + 1 : kp - 1;
    int kg1 = (cc % 2 == 0) ? kp - 1 : kp + 1;
    int kg2 = (cc % 2 == 0) ? kp - 2 : kp + 2;

    // surface types of surfaces forming edge
    string surfI = (cc <= 1) ? "il" : "iu";
    string surfK = (cc % 2 == 0) ? "kl" : "ku";

    // face indices
    // these only change from cell indices for upper edges
    // these should only be used for accessing faces in their corresponding
    // direction - i.e. ipF should only be used to access fAreaI, or fCenterI
    int ipF = (cc <= 1) ? numGhosts_ : this->NumI() + numGhosts_;
    int kpF = (cc % 2 == 0) ? numGhosts_ : this->NumK() + numGhosts_;

    // Assign states
    for (int jj = jmin; jj <= jmax; jj++) {
      // boundary conditions at corner
      string bc_I = bc_.GetBCName(ipF - numGhosts_, jj - numGhosts_,
                                  kp - numGhosts_, surfI);
      bc_I = (bc_I == "viscousWall") ? "slipWall" : bc_I;
      string bc_K = bc_.GetBCName(ip - numGhosts_, jj - numGhosts_,
                                  kpF - numGhosts_, surfK);
      bc_K = (bc_K == "viscousWall") ? "slipWall" : bc_K;

      // Assign states
      // i surface is a wall, but k surface is not - extend wall bc
      if (bc_I == "slipWall" && bc_K != "slipWall") {
        state_(ig1, jj, kg1) = state_(ip, jj, kg1).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jj, kg1), surfI, inp, eos, suth, 1);
        state_(ig1, jj, kg2) = state_(ip, jj, kg2).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jj, kg2), surfI, inp, eos, suth, 1);
        state_(ig2, jj, kg1) = state_(ii1, jj, kg1).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jj, kg1), surfI, inp, eos, suth, 2);
        state_(ig2, jj, kg2) = state_(ii1, jj, kg2).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jj, kg2), surfI, inp, eos, suth, 2);
        // k surface is a wall, but i surface is not - extend wall bc
      } else if (bc_I != "slipWall" && bc_K == "slipWall") {
        state_(ig1, jj, kg1) = state_(ig1, jj, kp).GetGhostState(
            bc_K, this->FAreaUnitK(ig1, jj, kpF), surfK, inp, eos, suth, 1);
        state_(ig2, jj, kg1) = state_(ig2, jj, kp).GetGhostState(
            bc_K, this->FAreaUnitK(ig2, jj, kpF), surfK, inp, eos, suth, 1);
        state_(ig1, jj, kg2) = state_(ig1, jj, ki1).GetGhostState(
            bc_K, this->FAreaUnitK(ig1, jj, kpF), surfK, inp, eos, suth, 2);
        state_(ig2, jj, kg2) = state_(ig2, jj, ki1).GetGhostState(
            bc_K, this->FAreaUnitK(ig2, jj, kpF), surfK, inp, eos, suth, 2);
      } else {  // both surfaces or neither are walls - proceed as normal
        state_(ig1, jj, kg1) = 0.5 * (state_(ip, jj, kg1) +
                                      state_(ig1, jj, kp));
        state_(ig1, jj, kg2) = state_(ip, jj, kg2);
        state_(ig2, jj, kg1) = state_(ig2, jj, kp);
        state_(ig2, jj, kg2) = 0.5 * (state_(ig1, jj, kg2) +
                                      state_(ig2, jj, kg1));
      }
    }
  }

  // ------------------------------------------------------------------------
  // loop over 4 edges that run in k-direction
  for (int cc = 0; cc < 4; cc++) {
    // cc = 0 -> il/jl
    // cc = 1 -> il/ju
    // cc = 2 -> iu/jl
    // cc = 3 -> iu/ju

    // cell indices
    int kmin = numGhosts_;
    int kmax = this->NumK() + numGhosts_ - 1;

    int ip = (cc <= 1) ? numGhosts_ : this->NumI() + numGhosts_ - 1;
    int ii1 = (cc <= 1) ? ip + 1 : ip - 1;
    int ig1 = (cc <= 1) ? ip - 1 : ip + 1;
    int ig2 = (cc <= 1) ? ip - 2 : ip + 2;

    int jp = (cc % 2 == 0) ? numGhosts_ : this->NumJ() + numGhosts_ - 1;
    int ji1 = (cc % 2 == 0) ? jp + 1 : jp - 1;
    int jg1 = (cc % 2 == 0) ? jp - 1 : jp + 1;
    int jg2 = (cc % 2 == 0) ? jp - 2 : jp + 2;

    // surface types of surfaces forming edge
    string surfI = (cc <= 1) ? "il" : "iu";
    string surfJ = (cc % 2 == 0) ? "jl" : "ju";

    // face indices
    // these only change from cell indices for upper edges
    // these should only be used for accessing faces in their corresponding
    // direction - i.e. ipF should only be used to access fAreaI, or fCenterI
    int ipF = (cc <= 1) ? numGhosts_ : this->NumI() + numGhosts_;
    int jpF = (cc % 2 == 0) ? numGhosts_ : this->NumJ() + numGhosts_;

    // Assign states
    for (int kk = kmin; kk <= kmax; kk++) {
      // boundary conditions at corner
      string bc_I = bc_.GetBCName(ipF - numGhosts_, jp - numGhosts_,
                           kk - numGhosts_, surfI);
      bc_I = (bc_I == "viscousWall") ? "slipWall" : bc_I;
      string bc_J = bc_.GetBCName(ip - numGhosts_, jpF - numGhosts_,
                           kk - numGhosts_, surfJ);
      bc_J = (bc_J == "viscousWall") ? "slipWall" : bc_J;

      // Assign states
      // i surface is a wall, but j surface is not - extend wall bc
      if (bc_I == "slipWall" && bc_J != "slipWall") {
        state_(ig1, jg1, kk) = state_(ip, jg1, kk).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jg1, kk), surfI, inp, eos, suth, 1);
        state_(ig1, jg2, kk) = state_(ip, jg2, kk).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jg2, kk), surfI, inp, eos, suth, 1);
        state_(ig2, jg1, kk) = state_(ii1, jg1, kk).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jg1, kk), surfI, inp, eos, suth, 2);
        state_(ig2, jg2, kk) = state_(ii1, jg2, kk).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jg2, kk), surfI, inp, eos, suth, 2);
        // j surface is a wall, but i surface is not - extend wall bc
      } else if (bc_I != "slipWall" && bc_J == "slipWall") {
        state_(ig1, jg1, kk) = state_(ig1, jp, kk).GetGhostState(
            bc_J, this->FAreaUnitJ(ig1, jpF, kk), surfJ, inp, eos, suth, 1);
        state_(ig2, jg1, kk) = state_(ig2, jp, kk).GetGhostState(
            bc_J, this->FAreaUnitJ(ig2, jpF, kk), surfJ, inp, eos, suth, 1);
        state_(ig1, jg2, kk) = state_(ig1, ji1, kk).GetGhostState(
            bc_J, this->FAreaUnitJ(ig1, jpF, kk), surfJ, inp, eos, suth, 2);
        state_(ig2, jg2, kk) = state_(ig2, ji1, kk).GetGhostState(
            bc_J, this->FAreaUnitJ(ig2, jpF, kk), surfJ, inp, eos, suth, 2);
      } else {  // both surfaces or neither are walls - proceed as normal
        state_(ig1, jg1, kk) = 0.5 * (state_(ip, jg1, kk) +
                                      state_(ig1, jp, kk));
        state_(ig1, jg2, kk) = state_(ip, jg2, kk);
        state_(ig2, jg1, kk) = state_(ig2, jp, kk);
        state_(ig2, jg2, kk) = 0.5 * (state_(ig1, jg2, kk) +
                                      state_(ig2, jg1, kk));
      }
    }
  }
  
  // DEBUG
  cout << state_;
  exit(0);

}

/* Member function to assign ghost cells for the viscous flow calculation. This
 function assumes AssignInviscidGhostCells has been run first as
 it only overwrites the ghost cells associated with the viscousWall boundary
 condition. It overwrites both regular and edge ghost cells.
*/
void procBlock::AssignViscousGhostCells(const input &inp, const idealGas &eos,
                                        const sutherland &suth) {
  // inp -- all input variables
  // eos -- equation of state
  // suth -- sutherland's law for viscosity

  // loop over all boundary surfaces
  for (int ii = 0; ii < bc_.NumSurfaces(); ii++) {
    // Get surface boundaries, and adjust them for ghost cells
    int imin = bc_.GetIMin(ii) - 1 + numGhosts_;
    int imax = bc_.GetIMax(ii) - 2 + numGhosts_;
    int jmin = bc_.GetJMin(ii) - 1 + numGhosts_;
    int jmax = bc_.GetJMax(ii) - 2 + numGhosts_;
    int kmin = bc_.GetKMin(ii) - 1 + numGhosts_;
    int kmax = bc_.GetKMax(ii) - 2 + numGhosts_;

    int g1, g2, i1, i2;  // indices for cells
    int bnd;  // indices for faces
    if (bc_.GetSurfaceType(ii) == 2) {  // upper i-surface
      g2 = imax + 2;
      g1 = imax + 1;
      i1 = imax;
      i2 = imax - 1;

      bnd = imax + 1;
    } else if (bc_.GetSurfaceType(ii) == 4) {  // upper j-surface
      g2 = jmax + 2;
      g1 = jmax + 1;
      i1 = jmax;
      i2 = jmax - 1;

      bnd = jmax + 1;
    } else if (bc_.GetSurfaceType(ii) == 6) {  // upper k-surface
      g2 = kmax + 2;
      g1 = kmax + 1;
      i1 = kmax;
      i2 = kmax - 1;

      bnd = kmax + 1;
    } else {  // lower surface
      g2 = 0;
      g1 = 1;
      i1 = 2;
      i2 = 3;

      bnd = 2;
    }

    //-----------------------------------------------------------------------
    // Assign ghost cells for i-surfaces
    // -----------------------------------------------------------------------
    // only overwrite cell values for viscous walls
    if ((bc_.GetSurfaceType(ii) == 1 || bc_.GetSurfaceType(ii) == 2) &&
        bc_.GetBCTypes(ii) == "viscousWall") {
      string surf = (bc_.GetSurfaceType(ii) == 1) ? "il" : "iu";
      string bcName = "viscousWall";
      // assign state for first layer of ghost cells
      multiArray3d<unitVec3dMag<double>> faceAreas =
          fAreaI_.Slice(bnd, bnd, jmin, jmax, kmin, kmax);
      multiArray3d<primVars> boundaryStates =
          state_.Slice(i1, i1, jmin, jmax, kmin, kmax);
      multiArray3d<primVars> ghostStates =
          GetGhostStates(boundaryStates, bcName, faceAreas, surf, inp, eos,
                         suth, 1);

      state_.Insert(g1, g1, jmin, jmax, kmin, kmax, ghostStates);

      // assign state for second layer of ghost cells
      // one cell thick - use one cell for both ghost cells
      if (this->NumI() < 2) {
        state_.Insert(g2, g2, jmin, jmax, kmin, kmax, ghostStates);
      } else {
        boundaryStates = state_.Slice(i2, i2, jmin, jmax, kmin, kmax);
        ghostStates = GetGhostStates(boundaryStates, bcName, faceAreas, surf,
                                     inp, eos, suth, 2);

        state_.Insert(g2, g2, jmin, jmax, kmin, kmax, ghostStates);
      }

    //-----------------------------------------------------------------------
    // Assign ghost cells for j-surfaces
    // -----------------------------------------------------------------------
    // only overwrite cell values for viscous walls
    } else if ((bc_.GetSurfaceType(ii) == 3 || bc_.GetSurfaceType(ii) == 4) &&
               bc_.GetBCTypes(ii) == "viscousWall") {
      string surf = (bc_.GetSurfaceType(ii) == 3) ? "jl" : "ju";
      string bcName = "viscousWall";
      // assign state for first layer of ghost cells
      multiArray3d<unitVec3dMag<double>> faceAreas =
          fAreaJ_.Slice(imin, imax, bnd, bnd, kmin, kmax);
      multiArray3d<primVars> boundaryStates =
          state_.Slice(imin, imax, i1, i1, kmin, kmax);
      multiArray3d<primVars> ghostStates =
          GetGhostStates(boundaryStates, bcName, faceAreas, surf, inp, eos,
                         suth, 1);

      // assign state for first layer of ghost cells
      state_.Insert(imin, imax, g1, g1, kmin, kmax, ghostStates);

      // assign state for second layer of ghost cells
      // one cell thick - use one cell for both ghost cells
      if (this->NumJ() < 2) {
        state_.Insert(imin, imax, g2, g2, kmin, kmax, ghostStates);
      } else {
        boundaryStates = state_.Slice(imin, imax, i2, i2, kmin, kmax);
        ghostStates = GetGhostStates(boundaryStates, bcName, faceAreas, surf,
                                     inp, eos, suth, 2);

        state_.Insert(imin, imax, g2, g2, kmin, kmax, ghostStates);
      }

    //-----------------------------------------------------------------------
    // Assign ghost cells for k-surfaces
    // -----------------------------------------------------------------------
    // only overwrite cell values for viscous walls
    } else if ((bc_.GetSurfaceType(ii) == 5 || bc_.GetSurfaceType(ii) == 6) &&
               bc_.GetBCTypes(ii) == "viscousWall") {
      string surf = (bc_.GetSurfaceType(ii) == 5) ? "kl" : "ku";
      string bcName = "viscousWall";
      // assign state for first layer of ghost cells
      multiArray3d<unitVec3dMag<double>> faceAreas =
          fAreaK_.Slice(imin, imax, jmin, jmax, bnd, bnd);
      multiArray3d<primVars> boundaryStates =
          state_.Slice(imin, imax, jmin, jmax, i1, i1);
      multiArray3d<primVars> ghostStates =
          GetGhostStates(boundaryStates, bcName, faceAreas, surf, inp, eos,
                         suth, 1);

      // assign state for first layer of ghost cells
      state_.Insert(imin, imax, jmin, jmax, g1, g1, ghostStates);

      // assign state for second layer of ghost cells
      // one cell thick - use one cell for both ghost cells
      if (this->NumK() < 2) {
        state_.Insert(imin, imax, jmin, jmax, g2, g2, ghostStates);
      } else {
        boundaryStates = state_.Slice(imin, imax, jmin, jmax, i2, i2);
        ghostStates = GetGhostStates(boundaryStates, bcName, faceAreas, surf,
                                     inp, eos, suth, 2);

        state_.Insert(imin, imax, jmin, jmax, g2, g2, ghostStates);
      }
    }
  }

  // Assign edge ghost cells
  this->AssignViscousGhostCellsEdge(inp, eos, suth);
}

/* Member function to assign values to ghost cells located on the 12 block edges
for the viscous flux calculation. Assumes AssignViscousGhostCells has already
been run. Only overwrites edge ghost cells if one of the boundaries at the corner
is a viscousWall boundary condition.
           ____ ____ ____ ____ ____ ____ ____ ____
          | G2 | G1 | X  | X  | X  | X  | X  | X  |
          |____|____|____|____|____|____|____|____|
         K| G2 | G1 | X  | X  | X  | X  | X  | X  |
         ^|____|____|____|____|____|____|____|____|
         || G2 | G1 | X  | X  | X  | X  | X  | X  |
         ||____|____|____|____|____|____|____|____|
         e| G2 | G1 | X* | X  | X  | X  | X  | X  |
          |____|____|____|____|____|____|____|____|
         1| E  | E  | G1 | G1 | G1 | G1 | G1 | G1 |
          |____|____|____|____|____|____|____|____|
         2| E  | E  | G2 | G2 | G2 | G2 | G2 | G2 |
          |____|____|____|____|____|____|____|____|
            2    1     e ----> J

In the above diagram the cells marked X represent physical cells. Cells marked
G1 and G2 represent the first and second layer of ghost cells respectively. The
cells marked E are the edge ghost cells that need to be assigned values. At each
corner location (X*) there are 4 edge ghost cells that need to be filled. The
axes on the side of the diagram indicate the coordinates of the edge ghost cells
(1, 2) as well as the coordinates of the adjacent regualar ghost cells (e).

The values at edge cell 1,1 are the average of the values at the two ghost cells
it touches at level "e". The values at edge cells 1,2 and 2,1 are identical to
the values of the ghost cells they tough at level "e". The values at edge cell
2,2 are the average of the values at the two (1,2 & 2,1) edge ghost cells it
touches. The exception to this rule occurs when either of the boundaries that
meet at the corner are viscousWall boundaries and the other is not. When this
occurs the viscousWall boundaries are "extended" into the ghost cells. This
implementation is described in Blazek.
*/
void procBlock::AssignViscousGhostCellsEdge(const input &inp,
                                            const idealGas &eos,
                                            const sutherland &suth) {
  // inp -- all input variables
  // eos -- equation of state
  // suth -- sutherland's law for viscosity

  // ------------------------------------------------------------------------
  // loop over 4 edges that run in i-direction
  for (int cc = 0; cc < 4; cc++) {
    // cc = 0 -> jl/kl
    // cc = 1 -> jl/ku
    // cc = 2 -> ju/kl
    // cc = 3 -> ju/ku

    // cell indices
    int imin = numGhosts_;
    int imax = this->NumI() + numGhosts_ - 1;

    int jp = (cc <= 1) ? numGhosts_ : this->NumJ() + numGhosts_ - 1;
    int ji1 = (cc <= 1) ? jp + 1 : jp - 1;
    int jg1 = (cc <= 1) ? jp - 1 : jp + 1;
    int jg2 = (cc <= 1) ? jp - 2 : jp + 2;

    int kp = (cc % 2 == 0) ? numGhosts_ : this->NumK() + numGhosts_ - 1;
    int ki1 = (cc % 2 == 0) ? kp + 1 : kp - 1;
    int kg1 = (cc % 2 == 0) ? kp - 1 : kp + 1;
    int kg2 = (cc % 2 == 0) ? kp - 2 : kp + 2;

    // surface types of surfaces forming edge
    string surfJ = (cc <= 1) ? "jl" : "ju";
    string surfK = (cc % 2 == 0) ? "kl" : "ku";

    // face indices
    // these only change from cell indices for upper edges
    // these should only be used for accessing faces in their corresponding
    // direction - i.e. jpF should only be used to access fAreaJ, or fCenterJ
    int jpF = (cc <= 1) ? numGhosts_ : this->NumJ() + numGhosts_;
    int kpF = (cc % 2 == 0) ? numGhosts_ : this->NumK() + numGhosts_;

    for (int ii = imin; ii <= imax; ii++) {
      // boundary conditions at corner
      string bc_J = bc_.GetBCName(ii - numGhosts_, jpF - numGhosts_,
                                  kp - numGhosts_, surfJ);
      string bc_K = bc_.GetBCName(ii - numGhosts_, jp - numGhosts_,
                                  kpF - numGhosts_, surfK);

      // Assign states
      // j surface is a wall, but k surface is not - extend wall bc
      if (bc_J == "viscousWall" && bc_K != "viscousWall") {
        state_(ii, jg1, kg1) = state_(ii, jp, kg1).GetGhostState(
            bc_J, this->FAreaUnitJ(ii, jpF, kg1), surfJ, inp, eos, suth, 1);
        state_(ii, jg1, kg2) = state_(ii, jp, kg2).GetGhostState(
            bc_J, this->FAreaUnitJ(ii, jpF, kg2), surfJ, inp, eos, suth, 1);
        state_(ii, jg2, kg1) = state_(ii, ji1, kg1).GetGhostState(
            bc_J, this->FAreaUnitJ(ii, jpF, kg1), surfJ, inp, eos, suth, 2);
        state_(ii, jg2, kg2) = state_(ii, ji1, kg2).GetGhostState(
            bc_J, this->FAreaUnitJ(ii, jpF, kg2), surfJ, inp, eos, suth, 2);
        // k surface is a wall, but j surface is not - extend wall bc
      } else if (bc_J != "viscousWall" && bc_K == "viscousWall") {
        state_(ii, jg1, kg1) = state_(ii, jg1, kp).GetGhostState(
            bc_K, this->FAreaUnitK(ii, jg1, kpF), surfK, inp, eos, suth, 1);
        state_(ii, jg2, kg1) = state_(ii, jg2, kp).GetGhostState(
            bc_K, this->FAreaUnitK(ii, jg2, kpF), surfK, inp, eos, suth, 1);
        state_(ii, jg1, kg2) = state_(ii, jg1, ki1).GetGhostState(
            bc_K, this->FAreaUnitK(ii, jg1, kpF), surfK, inp, eos, suth, 2);
        state_(ii, jg2, kg2) = state_(ii, jg2, ki1).GetGhostState(
            bc_K, this->FAreaUnitK(ii, jg2, kpF), surfK, inp, eos, suth, 2);
      // both surfaces are walls - proceed as normal
      } else if (bc_J == "viscousWall" && bc_K == "viscousWall") {
        state_(ii, jg1, kg1) = 0.5 * (state_(ii, jp, kg1) +
                                      state_(ii, jg1, kp));
        state_(ii, jg1, kg2) = state_(ii, jp, kg2);
        state_(ii, jg2, kg1) = state_(ii, jg2, kp);
        state_(ii, jg2, kg2) = 0.5 * (state_(ii, jg1, kg2) +
                                      state_(ii, jg2, kg1));
      }
      // if neither surfaces are walls, do nothing
    }
  }

  // ------------------------------------------------------------------------
  // loop over 4 edges that run in j-direction
  for (int cc = 0; cc < 4; cc++) {
    // cc = 0 -> il/kl
    // cc = 1 -> il/ku
    // cc = 2 -> iu/kl
    // cc = 3 -> iu/ku

    // cell indices
    int jmin = numGhosts_;
    int jmax = this->NumJ() + numGhosts_ - 1;

    int ip = (cc <= 1) ? numGhosts_ : this->NumI() + numGhosts_ - 1;
    int ii1 = (cc <= 1) ? ip + 1 : ip - 1;
    int ig1 = (cc <= 1) ? ip - 1 : ip + 1;
    int ig2 = (cc <= 1) ? ip - 2 : ip + 2;

    int kp = (cc % 2 == 0) ? numGhosts_ : this->NumK() + numGhosts_ - 1;
    int ki1 = (cc % 2 == 0) ? kp + 1 : kp - 1;
    int kg1 = (cc % 2 == 0) ? kp - 1 : kp + 1;
    int kg2 = (cc % 2 == 0) ? kp - 2 : kp + 2;

    // surface types of surfaces forming edge
    string surfI = (cc <= 1) ? "il" : "iu";
    string surfK = (cc % 2 == 0) ? "kl" : "ku";

    // face indices
    // these only change from cell indices for upper edges
    // these should only be used for accessing faces in their corresponding
    // direction - i.e. ipF should only be used to access fAreaI, or fCenterI
    int ipF = (cc <= 1) ? numGhosts_ : this->NumI() + numGhosts_;
    int kpF = (cc % 2 == 0) ? numGhosts_ : this->NumK() + numGhosts_;

    // Assign states
    for (int jj = jmin; jj <= jmax; jj++) {
      // boundary conditions at corner
      string bc_I = bc_.GetBCName(ipF - numGhosts_, jj - numGhosts_,
                                  kp - numGhosts_, surfI);
      string bc_K = bc_.GetBCName(ip - numGhosts_, jj - numGhosts_,
                                  kpF - numGhosts_, surfK);

      // Assign states
      // i surface is a wall, but k surface is not - extend wall bc
      if (bc_I == "viscousWall" && bc_K != "viscousWall") {
        state_(ig1, jj, kg1) = state_(ip, jj, kg1).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jj, kg1), surfI, inp, eos, suth, 1);
        state_(ig1, jj, kg2) = state_(ip, jj, kg2).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jj, kg2), surfI, inp, eos, suth, 1);
        state_(ig2, jj, kg1) = state_(ii1, jj, kg1).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jj, kg1), surfI, inp, eos, suth, 2);
        state_(ig2, jj, kg2) = state_(ii1, jj, kg2).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jj, kg2), surfI, inp, eos, suth, 2);
        // k surface is a wall, but i surface is not - extend wall bc
      } else if (bc_I != "viscousWall" && bc_K == "viscousWall") {
        state_(ig1, jj, kg1) = state_(ig1, jj, kp).GetGhostState(
            bc_K, this->FAreaUnitK(ig1, jj, kpF), surfK, inp, eos, suth, 1);
        state_(ig2, jj, kg1) = state_(ig2, jj, kp).GetGhostState(
            bc_K, this->FAreaUnitK(ig2, jj, kpF), surfK, inp, eos, suth, 1);
        state_(ig1, jj, kg2) = state_(ig1, jj, ki1).GetGhostState(
            bc_K, this->FAreaUnitK(ig1, jj, kpF), surfK, inp, eos, suth, 2);
        state_(ig2, jj, kg2) = state_(ig2, jj, ki1).GetGhostState(
            bc_K, this->FAreaUnitK(ig2, jj, kpF), surfK, inp, eos, suth, 2);
      // both surfaces are walls - proceed as normal
      } else if (bc_I == "viscousWall" && bc_K == "viscousWall") {
        state_(ig1, jj, kg1) = 0.5 * (state_(ip, jj, kg1) +
                                      state_(ig1, jj, kp));
        state_(ig1, jj, kg2) = state_(ip, jj, kg2);
        state_(ig2, jj, kg1) = state_(ig2, jj, kp);
        state_(ig2, jj, kg2) = 0.5 * (state_(ig1, jj, kg2) +
                                      state_(ig2, jj, kg1));
      }
      // if neither surfaces are walls, do nothing
    }
  }

  // ------------------------------------------------------------------------
  // loop over 4 edges that run in k-direction
  for (int cc = 0; cc < 4; cc++) {
    // cc = 0 -> il/jl
    // cc = 1 -> il/ju
    // cc = 2 -> iu/jl
    // cc = 3 -> iu/ju

    // cell indices
    int kmin = numGhosts_;
    int kmax = this->NumK() + numGhosts_ - 1;

    int ip = (cc <= 1) ? numGhosts_ : this->NumI() + numGhosts_ - 1;
    int ii1 = (cc <= 1) ? ip + 1 : ip - 1;
    int ig1 = (cc <= 1) ? ip - 1 : ip + 1;
    int ig2 = (cc <= 1) ? ip - 2 : ip + 2;

    int jp = (cc % 2 == 0) ? numGhosts_ : this->NumJ() + numGhosts_ - 1;
    int ji1 = (cc % 2 == 0) ? jp + 1 : jp - 1;
    int jg1 = (cc % 2 == 0) ? jp - 1 : jp + 1;
    int jg2 = (cc % 2 == 0) ? jp - 2 : jp + 2;

    // surface types of surfaces forming edge
    string surfI = (cc <= 1) ? "il" : "iu";
    string surfJ = (cc % 2 == 0) ? "jl" : "ju";

    // face indices
    // these only change from cell indices for upper edges
    // these should only be used for accessing faces in their corresponding
    // direction - i.e. ipF should only be used to access fAreaI, or fCenterI
    int ipF = (cc <= 1) ? numGhosts_ : this->NumI() + numGhosts_;
    int jpF = (cc % 2 == 0) ? numGhosts_ : this->NumJ() + numGhosts_;

    // Assign states
    for (int kk = kmin; kk <= kmax; kk++) {
      // boundary conditions at corner
      string bc_I = bc_.GetBCName(ipF - numGhosts_, jp - numGhosts_,
                           kk - numGhosts_, surfI);
      string bc_J = bc_.GetBCName(ip - numGhosts_, jpF - numGhosts_,
                           kk - numGhosts_, surfJ);

      // Assign states
      // i surface is a wall, but j surface is not - extend wall bc
      if (bc_I == "viscousWall" && bc_J != "viscousWall") {
        state_(ig1, jg1, kk) = state_(ip, jg1, kk).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jg1, kk), surfI, inp, eos, suth, 1);
        state_(ig1, jg2, kk) = state_(ip, jg2, kk).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jg2, kk), surfI, inp, eos, suth, 1);
        state_(ig2, jg1, kk) = state_(ii1, jg1, kk).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jg1, kk), surfI, inp, eos, suth, 2);
        state_(ig2, jg2, kk) = state_(ii1, jg2, kk).GetGhostState(
            bc_I, this->FAreaUnitI(ipF, jg2, kk), surfI, inp, eos, suth, 2);
        // j surface is a wall, but i surface is not - extend wall bc
      } else if (bc_I != "viscousWall" && bc_J == "viscousWall") {
        state_(ig1, jg1, kk) = state_(ig1, jp, kk).GetGhostState(
            bc_J, this->FAreaUnitJ(ig1, jpF, kk), surfJ, inp, eos, suth, 1);
        state_(ig2, jg1, kk) = state_(ig2, jp, kk).GetGhostState(
            bc_J, this->FAreaUnitJ(ig2, jpF, kk), surfJ, inp, eos, suth, 1);
        state_(ig1, jg2, kk) = state_(ig1, ji1, kk).GetGhostState(
            bc_J, this->FAreaUnitJ(ig1, jpF, kk), surfJ, inp, eos, suth, 2);
        state_(ig2, jg2, kk) = state_(ig2, ji1, kk).GetGhostState(
            bc_J, this->FAreaUnitJ(ig2, jpF, kk), surfJ, inp, eos, suth, 2);
      // both surfaces are walls - proceed as normal
      } else if (bc_I == "viscousWall" && bc_J == "viscousWall") {
        state_(ig1, jg1, kk) = 0.5 * (state_(ip, jg1, kk) +
                                      state_(ig1, jp, kk));
        state_(ig1, jg2, kk) = state_(ip, jg2, kk);
        state_(ig2, jg1, kk) = state_(ig2, jp, kk);
        state_(ig2, jg2, kk) = 0.5 * (state_(ig1, jg2, kk) +
                                      state_(ig2, jg1, kk));
      }
      // if neither surfaces are walls, do nothing
    }
  }
}

/* Member function to determine where in padded plot3dBlock an index is located.
It takes in an i, j, k cell location and returns a boolean indicating
if the given i, j, k location corresponds to a physical cell location.
 */
bool procBlock::IsPhysical(const int &ii, const int &jj, const int &kk,
                           const bool &includeGhost) const {
  // ii -- i index of location to test
  // jj -- j index of location to test
  // kk -- k index of location to test
  // includeGhost -- flag to determine if inputs include ghost cells or not

  bool isPhysical = true;

  int offset = includeGhost ? numGhosts_ : 0;

  // if any of (i, j, & k) are outside of the limits of physical cells, location
  // is non-physical
  if ((ii < offset ||
       ii > this->NumI() - 1 + offset) ||
      (jj < offset ||
       jj > this->NumJ() - 1 + offset) ||
      (kk < offset ||
       kk > this->NumK() - 1 + offset)) {
    isPhysical = false;
  }

  return isPhysical;
}

/* Member function to determine where in padded plot3dBlock an index is located.
It takes in an i, j, k cell location and returns a boolean indicating
if the given i, j, k location corresponds to a corner location. Corner locations
are not used at all. This function is NOT USED in the code but is useful
for debugging purposes.
 */
bool procBlock::AtCorner(const int &ii, const int &jj, const int &kk,
                         const bool &includeGhost) const {
  // ii -- i index of location to test
  // jj -- j index of location to test
  // kk -- k index of location to test
  // includeGhost -- flag to determine if inputs include ghost cells or not

  bool atCorner = false;

  int offset = includeGhost ? numGhosts_ : 0;

  // if all (i, j, & k) are outside of the limits of physical cells, location is
  // a corner location
  if ((ii < offset ||
       ii > this->NumI() - 1 + offset) &&
      (jj < offset ||
       jj > this->NumJ() - 1 + offset) &&
      (kk < offset ||
       kk > this->NumK() - 1 + offset)) {
    atCorner = true;
  }

  return atCorner;
}

/* Member function to determine where in padded plot3dBlock an index is located.
It takes in an i, j, k cell location and returns a boolean indicating
if the given i, j, k location corresponds to a edge location. Edge locations are
used in the gradient calculations.
 */
bool procBlock::AtEdge(const int &ii, const int &jj, const int &kk,
                       const bool &includeGhost, string &dir) const {
  // ii -- i index of location to test
  // jj -- j index of location to test
  // kk -- k index of location to test
  // includeGhost -- flag to determine if inputs include ghost cells or not
  // dir -- direction that edge runs in

  bool atEdge = false;

  int offset = includeGhost ? numGhosts_ : 0;

  if ((ii >= offset &&
       ii < this->NumI() + offset) &&  // at i-edge - i in
                                         // physical cell range,
                                         // j/k at first level of
                                         // ghost cells
      (jj == offset - 1 ||
       jj == this->NumJ() + offset) &&
      (kk == offset - 1 ||
       kk == this->NumK() + offset)) {
    atEdge = true;
    dir = "i";
  } else if ((ii == offset - 1 ||
              ii == this->NumI() +
                        offset) &&  // at j-edge - j in physical
                                    // cell range, i/k at first
                                    // level of ghost cells
             (jj >= offset &&
              jj < this->NumJ() + offset) &&
             (kk == offset - 1 ||
              kk == this->NumK() + offset)) {
    atEdge = true;
    dir = "j";
  } else if ((ii == offset - 1 ||
              ii == this->NumI() +
                        offset) &&  // at k-edge - k in physical
                                    // cell range, i/j at first
                                    // level of ghost cells
             (jj == offset - 1 ||
              jj == this->NumJ() + offset) &&
             (kk >= offset &&
              kk < this->NumK() + offset)) {
    atEdge = true;
    dir = "k";
  }

  return atEdge;
}

/* Function to swap ghost cell geometry between two blocks at an interblock
boundary. Slices are removed from the physical cells (extending into ghost cells
at the edges) of one block and inserted into the ghost cells of its partner
block. The reverse is also true. The slices are taken in the coordinate system
orientation of their parent block.

   Interior Cells    Ghost Cells               Ghost Cells   Interior Cells
   ________ ______|________ _________       _______________|_______ _________
Ui-3/2   Ui-1/2   |    Uj+1/2    Uj+3/2  Ui-3/2    Ui-1/2  |    Uj+1/2    Uj+3/2
  |        |      |        |         |     |        |      |       |         |
  | Ui-1   |  Ui  |  Uj    |  Uj+1   |     |  Ui-1  |   Ui |  Uj   |  Uj+1   |
  |        |      |        |         |     |        |      |       |         |
  |________|______|________|_________|     |________|______|_______|_________|
                  |                                        |

The above diagram shows the resulting values after the ghost cell swap. The
logic ensures that the ghost cells at the interblock boundary exactly match
their partner block as if there were no separation in the grid.

Only 3 faces at each ghost cell need to be swapped (i.e. the lower face for Ui
is the upper face for Ui-1). At the end of a line (i-line, j-line or k-line),
both the upper and lower faces need to be swapped.
*/
void SwapSlice(interblock &inter, procBlock &blk1, procBlock &blk2,
               const bool &geom) {
  // inter -- interblock boundary information
  // blk1 -- first block involved in interblock boundary
  // blk2 -- second block involved in interblock boundary
  // geom -- boolean to determine whether to swap geometry or states

  // Get indices for slice coming from first block to swap
  int is1, ie1, js1, je1, ks1, ke1;

  // if at upper boundary no need to adjust for ghost cells as constant surface
  // is already at the interior cells when acounting for ghost cells
  // if at the lower boundary adjust the constant surface by the number of ghost
  // cells to get to the first interior cell
  int upLowFac = (inter.BoundaryFirst() % 2 == 0) ? 0 : blk1.NumGhosts();

  if (inter.BoundaryFirst() == 1 ||
      inter.BoundaryFirst() == 2) {  // direction 3 is i
    // extend min/maxes to cover ghost cells
    is1 = inter.ConstSurfaceFirst() + upLowFac;
    ie1 = is1 + blk1.NumGhosts() - 1;

    // direction 1 is j
    js1 = inter.Dir1StartFirst();
    je1 = inter.Dir1EndFirst() - 1 + 2 * blk1.NumGhosts();

    // direction 2 is k
    ks1 = inter.Dir2StartFirst();
    ke1 = inter.Dir2EndFirst() - 1 + 2 * blk1.NumGhosts();
  } else if (inter.BoundaryFirst() == 3 ||
             inter.BoundaryFirst() == 4) {  // direction 3 is j
    // extend min/maxes to cover ghost cells
    js1 = inter.ConstSurfaceFirst() + upLowFac;
    je1 = js1 + blk1.NumGhosts() - 1;

    // direction 1 is k
    ks1 = inter.Dir1StartFirst();
    ke1 = inter.Dir1EndFirst() - 1 + 2 * blk1.NumGhosts();

    // direction 2 is i
    is1 = inter.Dir2StartFirst();
    ie1 = inter.Dir2EndFirst() - 1 + 2 * blk1.NumGhosts();
  } else if (inter.BoundaryFirst() == 5 ||
             inter.BoundaryFirst() == 6) {  // direction 3 is k
    // extend min/maxes to cover ghost cells
    ks1 = inter.ConstSurfaceFirst() + upLowFac;
    ke1 = ks1 + blk1.NumGhosts() - 1;

    // direction 1 is i
    is1 = inter.Dir1StartFirst();
    ie1 = inter.Dir1EndFirst() - 1 + 2 * blk1.NumGhosts();

    // direction 2 is j
    js1 = inter.Dir2StartFirst();
    je1 = inter.Dir2EndFirst() - 1 + 2 * blk1.NumGhosts();
  } else {
    cerr << "ERROR: Error in procBlock::SwapSlice(). Surface boundary "
         << inter.BoundaryFirst() << " is not recognized!" << endl;
    exit(0);
  }

  // Get indices for slice coming from second block to swap
  int is2, ie2, js2, je2, ks2, ke2;

  // if at upper boundary no need to adjust for ghost cells as constant surface
  // is already at the interior cells when acounting for ghost cells
  // if at the lower boundary adjust the constant surface by the number of ghost
  // cells to get to the first interior cell
  upLowFac = (inter.BoundarySecond() % 2 == 0) ? 0 : blk2.NumGhosts();

  if (inter.BoundarySecond() == 1 ||
      inter.BoundarySecond() == 2) {  // direction 3 is i
    // extend min/maxes to cover ghost cells
    is2 = inter.ConstSurfaceSecond() + upLowFac;
    ie2 = is2 + blk2.NumGhosts() - 1;

    // direction 1 is j
    js2 = inter.Dir1StartSecond();
    je2 = inter.Dir1EndSecond() - 1 + 2 * blk2.NumGhosts();

    // direction 2 is k
    ks2 = inter.Dir2StartSecond();
    ke2 = inter.Dir2EndSecond() - 1 + 2 * blk2.NumGhosts();
  } else if (inter.BoundarySecond() == 3 ||
             inter.BoundarySecond() == 4) {  // direction 3 is j
    // extend min/maxes to cover ghost cells
    js2 = inter.ConstSurfaceSecond() + upLowFac;
    je2 = js2 + blk2.NumGhosts() - 1;

    // direction 1 is k
    ks2 = inter.Dir1StartSecond();
    ke2 = inter.Dir1EndSecond() - 1 + 2 * blk2.NumGhosts();

    // direction 2 is i
    is2 = inter.Dir2StartSecond();
    ie2 = inter.Dir2EndSecond() - 1 + 2 * blk2.NumGhosts();
  } else if (inter.BoundarySecond() == 5 ||
             inter.BoundarySecond() == 6) {  // direction 3 is k
    // extend min/maxes to cover ghost cells
    ks2 = inter.ConstSurfaceSecond() + upLowFac;
    ke2 = ks2 + blk2.NumGhosts() - 1;

    // direction 1 is i
    is2 = inter.Dir1StartSecond();
    ie2 = inter.Dir1EndSecond() - 1 + 2 * blk2.NumGhosts();

    // direction 2 is j
    js2 = inter.Dir2StartSecond();
    je2 = inter.Dir2EndSecond() - 1 + 2 * blk2.NumGhosts();
  } else {
    cerr << "ERROR: Error in procBlock::SwapSlice(). Surface boundary "
         << inter.BoundarySecond() << " is not recognized!" << endl;
    exit(0);
  }

  geomSlice geom1, geom2;
  stateSlice state1, state2;
  if (geom) {  // get geomSlices to swap
    geom1 = geomSlice(blk1, is1, ie1, js1, je1, ks1, ke1);
    geom2 = geomSlice(blk2, is2, ie2, js2, je2, ks2, ke2);
  } else {  // get stateSlices to swap
    state1 = stateSlice(blk1, is1, ie1, js1, je1, ks1, ke1);
    state2 = stateSlice(blk2, is2, ie2, js2, je2, ks2, ke2);
  }

  // change interblocks to work with slice and ghosts
  interblock inter1 = inter;
  interblock inter2 = inter;
  inter1.AdjustForSlice(false, blk1.NumGhosts());
  inter2.AdjustForSlice(true, blk2.NumGhosts());

  // put slices in proper blocks
  if (geom) {  // put geomSlices in procBlock
    // return vector determining if any of the 4 edges of the interblock need to
    // be updated for a "t" intersection
    vector<bool> adjEdge1 =
        blk1.PutGeomSlice(geom2, inter2, blk2.NumGhosts(), blk2.NumGhosts());
    vector<bool> adjEdge2 =
        blk2.PutGeomSlice(geom1, inter1, blk1.NumGhosts(), blk1.NumGhosts());

    // if an interblock border needs to be updated, update
    for (unsigned int ii = 0; ii < adjEdge1.size(); ii++) {
      if (adjEdge1[ii]) {
        inter.UpdateBorderFirst(ii);
      }
      if (adjEdge2[ii]) {
        inter.UpdateBorderSecond(ii);
      }
    }
  } else {  // put stateSlices in procBlock
    blk1.PutStateSlice(state2, inter2, blk2.NumGhosts(), blk2.NumGhosts());
    blk2.PutStateSlice(state1, inter1, blk1.NumGhosts(), blk1.NumGhosts());
  }
}

/* Function to swap slice using MPI. This is similar to the SwapSlice member
function, but is called when the neighboring procBlocks are on different
processors.
*/
void procBlock::SwapSliceMPI(const interblock &inter, const int &rank,
                             const MPI_Datatype &MPI_cellData) {
  // inter -- interblock boundary information
  // rank -- processor rank
  // MPI_cellData -- MPI datatype for passing primVars, genArray

  // Get indices for slice coming from block to swap
  int is, ie, js, je, ks, ke;

  if (rank == inter.RankFirst()) {  // local block is first in interblock
    // if at upper boundary no need to adjust for ghost cells as constant
    // surface is already at the interior cells when acounting for ghost cells
    // if at the lower boundary adjust the constant surface by the number of
    // ghost cells to get to the first interior cell
    int upLowFac = (inter.BoundaryFirst() % 2 == 0) ? 0 : numGhosts_;

    if (inter.BoundaryFirst() == 1 ||
        inter.BoundaryFirst() == 2) {  // direction 3 is i
      // extend min/maxes to cover ghost cells
      is = inter.ConstSurfaceFirst() + upLowFac;
      ie = is + numGhosts_ - 1;

      // direction 1 is j
      js = inter.Dir1StartFirst();
      je = inter.Dir1EndFirst() - 1 + 2 * numGhosts_;

      // direction 2 is k
      ks = inter.Dir2StartFirst();
      ke = inter.Dir2EndFirst() - 1 + 2 * numGhosts_;
    } else if (inter.BoundaryFirst() == 3 ||
               inter.BoundaryFirst() == 4) {  // direction 3 is j
      // extend min/maxes to cover ghost cells
      js = inter.ConstSurfaceFirst() + upLowFac;
      je = js + numGhosts_ - 1;

      // direction 1 is k
      ks = inter.Dir1StartFirst();
      ke = inter.Dir1EndFirst() - 1 + 2 * numGhosts_;

      // direction 2 is i
      is = inter.Dir2StartFirst();
      ie = inter.Dir2EndFirst() - 1 + 2 * numGhosts_;
    } else if (inter.BoundaryFirst() == 5 ||
               inter.BoundaryFirst() == 6) {  // direction 3 is k
      // extend min/maxes to cover ghost cells
      ks = inter.ConstSurfaceFirst() + upLowFac;
      ke = ks + numGhosts_ - 1;

      // direction 1 is i
      is = inter.Dir1StartFirst();
      ie = inter.Dir1EndFirst() - 1 + 2 * numGhosts_;

      // direction 2 is j
      js = inter.Dir2StartFirst();
      je = inter.Dir2EndFirst() - 1 + 2 * numGhosts_;
    } else {
      cerr << "ERROR: Error in procBlock::SwapSliceMPI(). Surface boundary "
           << inter.BoundaryFirst() << " is not recognized!" << endl;
      exit(0);
    }
  // local block is second in interblock
  } else if (rank == inter.RankSecond()) {
    // if at upper boundary no need to adjust for ghost cells as constant
    // surface is already at the interior cells when acounting for ghost cells
    // if at the lower boundary adjust the constant surface by the number of
    // ghost cells to get to the first interior cell
    int upLowFac = (inter.BoundarySecond() % 2 == 0) ? 0 : numGhosts_;

    if (inter.BoundarySecond() == 1 ||
        inter.BoundarySecond() == 2) {  // direction 3 is i
      // extend min/maxes to cover ghost cells
      is = inter.ConstSurfaceSecond() + upLowFac;
      ie = is + numGhosts_ - 1;

      // direction 1 is j
      js = inter.Dir1StartSecond();
      je = inter.Dir1EndSecond() - 1 + 2 * numGhosts_;

      // direction 2 is k
      ks = inter.Dir2StartSecond();
      ke = inter.Dir2EndSecond() - 1 + 2 * numGhosts_;
    } else if (inter.BoundarySecond() == 3 ||
               inter.BoundarySecond() == 4) {  // direction 3 is j
      // extend min/maxes to cover ghost cells
      js = inter.ConstSurfaceSecond() + upLowFac;
      je = js + numGhosts_ - 1;

      // direction 1 is k
      ks = inter.Dir1StartSecond();
      ke = inter.Dir1EndSecond() - 1 + 2 * numGhosts_;

      // direction 2 is i
      is = inter.Dir2StartSecond();
      ie = inter.Dir2EndSecond() - 1 + 2 * numGhosts_;
    } else if (inter.BoundarySecond() == 5 ||
               inter.BoundarySecond() == 6) {  // direction 3 is k
      // extend min/maxes to cover ghost cells
      ks = inter.ConstSurfaceSecond() + upLowFac;
      ke = ks + numGhosts_ - 1;

      // direction 1 is i
      is = inter.Dir1StartSecond();
      ie = inter.Dir1EndSecond() - 1 + 2 * numGhosts_;

      // direction 2 is j
      js = inter.Dir2StartSecond();
      je = inter.Dir2EndSecond() - 1 + 2 * numGhosts_;
    } else {
      cerr << "ERROR: Error in procBlock::SwapSliceMPI(). Surface boundary "
           << inter.BoundarySecond() << " is not recognized!" << endl;
      exit(0);
    }
  } else {
    cerr << "ERROR: Error in procBlock::SwapSliceMPI(). Processor rank does "
            "not match either of interblock ranks!" << endl;
    exit(0);
  }

  // get local state slice to swap
  stateSlice state((*this), is, ie, js, je, ks, ke);

  // swap state slices with partner block
  state.PackSwapUnpackMPI(inter, MPI_cellData, rank);

  // change interblocks to work with slice and ghosts
  interblock interAdj = inter;

  // block to insert into is first in interblock
  if (rank == inter.RankSecond()) {
    interAdj.AdjustForSlice(false, numGhosts_);
  } else {  // block to insert into is second in interblock, so pass swapped
            // version
    interAdj.AdjustForSlice(true, numGhosts_);
  }

  // insert stateSlice into procBlock
  this->PutStateSlice(state, interAdj, numGhosts_, numGhosts_);
}

/* Function to return a vector of location indicies for ghost cells at an
interblock boundary. The vector is formatted as shown below:

  vector = [i j k]

The vector will contain 3 entries corresponding to the i, j, and k locations of
either the first or second pair in the interblock, depending on what is
specified in the pairID variable. The indices returned will correspond to cell
locations and will take into account the orientation of the patches that
comprise the interblock with relation to each other.
*/
vector3d<int> GetSwapLoc(const int &l1, const int &l2, const int &l3,
                         const interblock &inter, const bool &pairID) {
  // l1 -- index of direction 1 within slice to insert
  // l2 -- index of direction 2 within slice to insert
  // l3 -- index of direction 3 within slice to insert
  // inter -- interblock boundary condition
  // pairID -- returning index for first or second block in interblock match

  // preallocate vector to return
  vector3d<int> loc;

  if (pairID) {  // working on first in pair ------------------------------
    // first patch in pair is calculated using orientation 1
    if (inter.Direction3First() == "i") {  // i-patch
      // get direction 1 length
      loc[1] = inter.Dir1StartFirst() + l1;  // direction 1 is j
      loc[2] = inter.Dir2StartFirst() + l2;  // direction 2 is k
      loc[0] = inter.ConstSurfaceFirst() +
               l3;  // add l3 to get to ghost cells (cell index instead of face)
    } else if (inter.Direction3First() == "j") {  // j-patch
      // get direction 1 length
      loc[2] = inter.Dir1StartFirst() + l1;  // direction 1 is k
      loc[0] = inter.Dir2StartFirst() + l2;  // direction 2 is i
      loc[1] = inter.ConstSurfaceFirst() +
               l3;  // add l3 to get to ghost cells (cell index instead of face)
    } else if (inter.Direction3First() == "k") {  // k-patch
      // get direction 1 length
      loc[0] = inter.Dir1StartFirst() + l1;  // direction 1 is i
      loc[1] = inter.Dir2StartFirst() + l2;  // direction 2 is j
      loc[2] = inter.ConstSurfaceFirst() +
               l3;  // add l3 to get to ghost cells (cell index instead of face)
    } else {
      cerr << "ERROR: Error in procBlock:GetSwapLoc(). Boundary direction "
           << inter.Direction3First() << " is not recognized!" << endl;
      exit(0);
    }
  //--------------------------------------------------------------------------
  // need to use orientation for second in pair
  } else {  // working on second in pair ---------------------------------------
    if (inter.Direction3Second() == "i") {  // i-patch
      if (inter.Orientation() == 2 || inter.Orientation() == 4 ||
          inter.Orientation() == 5 ||
          inter.Orientation() == 7) {  // swap dir 1 and 2
        // direction 1 is j (swapped) -- if true direction reversed -- subtract
        // 1 from End to get to cell index
        loc[2] = (inter.Orientation() == 5 || inter.Orientation() == 7)
                     ? inter.Dir2EndSecond() - 1 - l1
                     : inter.Dir2StartSecond() + l1;

        // direction 2 is k (swapped) -- if true direction reversed -- subtract
        // 1 from End to get to cell index
        loc[1] = (inter.Orientation() == 4 || inter.Orientation() == 7)
                     ? inter.Dir1EndSecond() - 1 - l2
                     : inter.Dir1StartSecond() + l2;
      } else {  // no direction swap
        // direction 1 is j -- if true direction reversed -- subtract 1 from End
        // to get to cell index
        loc[1] = (inter.Orientation() == 6 || inter.Orientation() == 8)
                     ? inter.Dir1EndSecond() - 1 - l1
                     : inter.Dir1StartSecond() + l1;

        // direction 1 is k -- if true direction reversed -- subtract 1 from End
        // to get to cell index
        loc[2] = (inter.Orientation() == 3 || inter.Orientation() == 8)
                     ? inter.Dir2EndSecond() - 1 - l2
                     : inter.Dir2StartSecond() + l2;
      }

      // calculate index for all ghost layers
      loc[0] = inter.ConstSurfaceSecond() + l3;  // add l3 to get to ghost cells

    //-------------------------------------------------------------------------
    } else if (inter.Direction3Second() == "j") {  // j-patch
      if (inter.Orientation() == 2 || inter.Orientation() == 4 ||
          inter.Orientation() == 5 ||
          inter.Orientation() == 7) {  // swap dir 1 and 2
        // direction 1 is k (swapped) -- if true direction reversed -- subtract
        // 1 from End to get to cell index
        loc[0] = (inter.Orientation() == 5 || inter.Orientation() == 7)
                     ? inter.Dir2EndSecond() - 1 - l1
                     : inter.Dir2StartSecond() + l1;

        // direction 2 is i (swapped) -- if true direction reversed -- subtract
        // 1 from End to get to cell index
        loc[2] = (inter.Orientation() == 4 || inter.Orientation() == 7)
                     ? inter.Dir1EndSecond() - 1 - l2
                     : inter.Dir1StartSecond() + l2;
      } else {  // no direction swap
        // direction 1 is k -- if true direction reversed -- subtract 1 from End
        // to get to cell index
        loc[2] = (inter.Orientation() == 3 || inter.Orientation() == 8)
                     ? inter.Dir1EndSecond() - 1 - l1
                     : inter.Dir1StartSecond() + l1;

        // direction 2 is i -- if true direction reversed -- subtract 1 from End
        // to get to cell index
        loc[0] = (inter.Orientation() == 6 || inter.Orientation() == 8)
                     ? inter.Dir2EndSecond() - 1 - l2
                     : inter.Dir2StartSecond() + l2;
      }

      // calculate index for all ghost layers
      loc[1] = inter.ConstSurfaceSecond() + l3;  // add l3 to get to ghost cells

    //------------------------------------------------------------------------
    } else if (inter.Direction3Second() == "k") {  // k-patch
      if (inter.Orientation() == 2 || inter.Orientation() == 4 ||
          inter.Orientation() == 5 ||
          inter.Orientation() == 7) {  // swap dir 1 and 2
        // direction 1 is i (swapped) -- if true direction reversed -- subtract
        // 1 from End to get to cell index
        loc[1] = (inter.Orientation() == 5 || inter.Orientation() == 7)
                     ? inter.Dir2EndSecond() - 1 - l1
                     : inter.Dir2StartSecond() + l1;

        // direction 2 is j (swapped) -- if true direction reversed -- subtract
        // 1 from End to get to cell index
        loc[0] = (inter.Orientation() == 4 || inter.Orientation() == 7)
                     ? inter.Dir1EndSecond() - 1 - l2
                     : inter.Dir1StartSecond() + l2;
      } else {  // no direction swap
        // direction 1 is i -- if true direction reversed -- subtract 1 from End
        // to get to cell index
        loc[0] = (inter.Orientation() == 3 || inter.Orientation() == 8)
                     ? inter.Dir1EndSecond() - 1 - l1
                     : inter.Dir1StartSecond() + l1;

        // direction 2 is j -- if true direction reversed -- subtract 1 from End
        // to get to cell index
        loc[1] = (inter.Orientation() == 6 || inter.Orientation() == 8)
                     ? inter.Dir2EndSecond() - 1 - l2
                     : inter.Dir2StartSecond() + l2;
      }

      // calculate index for all ghost layers
      loc[2] = inter.ConstSurfaceSecond() + l3;  // add l3 to get to ghost cells

    //--------------------------------------------------------------------------
    } else {
      cerr << "ERROR: Error in procBlock.cpp:GetSwapLoc(). Boundary surface of "
           << inter.Direction3Second() << " is not recognized!" << endl;
      exit(0);
    }
  }

  return loc;
}

/* Function to populate ghost cells with proper cell states for inviscid flow
calculation. This function operates on the entire grid and uses interblock
boundaries to pass the correct data between grid blocks.
*/
void GetBoundaryConditions(vector<procBlock> &states, const input &inp,
                           const idealGas &eos, const sutherland &suth,
                           vector<interblock> &connections, const int &rank,
                           const MPI_Datatype &MPI_cellData) {
  // states -- vector of all procBlocks in the solution domain
  // inp -- all input variables
  // eos -- equation of state
  // suth -- sutherland's law for viscosity
  // connections -- vector of interblock connections

  // loop over all blocks and assign inviscid ghost cells
  for (unsigned int ii = 0; ii < states.size(); ii++) {
    states[ii].AssignInviscidGhostCells(inp, eos, suth);
  }

  // loop over connections and swap ghost cells where needed
  for (unsigned int ii = 0; ii < connections.size(); ii++) {
    if (connections[ii].RankFirst() == rank &&
        connections[ii].RankSecond() == rank) {  // both sides of interblock
                                                  // are on this processor, swap
                                                  // w/o mpi
      SwapSlice(connections[ii], states[connections[ii].LocalBlockFirst()],
                states[connections[ii].LocalBlockSecond()], false);
    } else if (connections[ii].RankFirst() ==
               rank) {  // rank matches rank of first side of interblock,
                         // swap over mpi
      states[connections[ii].LocalBlockFirst()]
          .SwapSliceMPI(connections[ii], rank, MPI_cellData);

    } else if (connections[ii].RankSecond() ==
               rank) {  // rank matches rank of second side of interblock,
                         // swap over mpi
      states[connections[ii].LocalBlockSecond()]
          .SwapSliceMPI(connections[ii], rank, MPI_cellData);
    }
    // if rank doesn't match either side of interblock, then do nothing and
    // move on to the next interblock
  }

  // loop over all blocks and get ghost cell edge data
  for (unsigned int ii = 0; ii < states.size(); ii++) {
    states[ii].AssignInviscidGhostCellsEdge(inp, eos, suth);
  }
}

/* Member function to overwrite a section of a procBlock's geometry with a
geomSlice. The function uses the orientation supplied in the interblock to
orient the geomSlice relative to the procBlock. It assumes that the procBlock
is listed first, and the geomSlice second in the interblock data structure.
It returns a vector of 4 bools that are returned true if one of the 4 edges
of the interblock need to be updated because they border an interblock,
creating a possible "t" intersection.
     __________     _________
    |       |  |   |  |      |
    |       |  |   |  |  2   |
    |       |  |   |__|______|
    |       |  |   |X_|______|
    |  0    |--|    _________
    |       |  |   |X_|______|
    |       |  |   |  |      |
    |       |  |   |  |  1   |
    |_______|__|   |__|______|

The block configuration shown above shows a "t"intersection. If blocks 0/1 are
swapped first, block 1 gets all of its ghost cells including the edge cells
(marked X) from block 0, but block 0 knows that the interblock swapping with
block 1 borders another interblock (the one swapping with block 2), so it does
not take the edge ghost cells from block 1. This is because the ghost cells that
the edge of block 1 would supply will be supplied by the swap with block 2. If
next, blocks 1/2 swap, block 1 now has garbage in its edge ghost cells. This is
because it is the first swap for block 2, so its ghost cells are empty (filled
with garbage). If next blocks 0/2 swap, block 0 does not take the ghost cells
from block 2 for the same reason it did not take them from block 1. This leaves
blocks 0 and 2 with the correct ghost cells, but block 1 has garbage in its edge
ghost cells. This problem occurs if the 1/2 swap is done between the 0/1 and
0/2 swaps!

To solve this problem, in this situation the 1/2 swap is done without edge ghost
cells. This fixes the problem because the correct ghost cells block 1 got from
block 0 are not overwritten with the garbage ghost cells from block 2. The way
this is enforced is the following. Initially all ghost cell volumes are
initialized to 0. Therefore, if a cell from a geomSlice with a volume of 0 is
trying to be inserted into the procBlock, this cell has not been given its proper
boundary conditions and should be ignored. Subsequently, the interblock should be
updated so that in the future this cell is not inserted.
*/
vector<bool> procBlock::PutGeomSlice(const geomSlice &slice, interblock &inter,
                                     const int &d3, const int &numG) {
  // slice -- geomSlice to insert int procBlock
  // inter -- interblock data structure describing the patches and their
  // orientation
  // d3 -- distance of direction normal to patch to insert
  // numG -- number of ghost cells

  // check that number of cells to insert matches
  int blkCell = (inter.Dir1EndFirst() - inter.Dir1StartFirst()) *
                (inter.Dir2EndFirst() - inter.Dir2StartFirst()) * d3;
  if (blkCell != slice.NumCells()) {
    cerr << "ERROR: Error in procBlock::PutGeomSlice(). Number of cells being "
            "inserted does not match designated space to insert to." << endl;
    cerr << "Direction 1, 2, 3 of procBlock: "
         << inter.Dir1EndFirst() - inter.Dir1StartFirst() << ", "
         << inter.Dir2EndFirst() - inter.Dir2StartFirst() << ", " << d3 << endl;
    cerr << "Direction I, J, K of geomSlice: " << slice.NumI() << ", "
         << slice.NumJ() << ", " << slice.NumK() << endl;
    exit(0);
  }

  // adjust insertion indices if patch borders another interblock on the same
  // surface of the block
  int adjS1 = (inter.Dir1StartInterBorderFirst()) ? numG : 0;
  int adjE1 = (inter.Dir1EndInterBorderFirst()) ? numG : 0;
  int adjS2 = (inter.Dir2StartInterBorderFirst()) ? numG : 0;
  int adjE2 = (inter.Dir2EndInterBorderFirst()) ? numG : 0;
  vector<bool> adjEdge(4, false);  // initialize all return values to false

  // determine if area direction needs to be reversed
  double aFac3 = ((inter.BoundaryFirst() + inter.BoundarySecond()) % 2 == 0)
      ? -1.0 : 1.0;
  double aFac1 = (inter.Orientation() == 3 || inter.Orientation() == 4 ||
                  inter.Orientation() == 7 || inter.Orientation() == 8)
      ? -1.0 : 1.0;
  double aFac2 = (inter.Orientation() == 5 || inter.Orientation() == 6 ||
                  inter.Orientation() == 7 || inter.Orientation() == 8)
      ? -1.0 : 1.0;

  // loop over cells to insert
  for (int l3 = 0; l3 < d3; l3++) {
    for (int l2 = adjS2;
         l2 < (inter.Dir2EndFirst() - inter.Dir2StartFirst() - adjE2); l2++) {
      for (int l1 = adjS1;
           l1 < (inter.Dir1EndFirst() - inter.Dir1StartFirst() - adjE1); l1++) {
        // get block and slice indices
        vector3d<int> indB = GetSwapLoc(l1, l2, l3, inter, true);
        vector3d<int> indS = GetSwapLoc(l1, l2, l3, inter, false);

        // don't overwrite with garbage from partner block that hasn't recieved
        // its ghost value yet (needed at "t" intersection)
        if (slice.Vol(indS[0], indS[1], indS[2]) == 0.0) {
          // find out if on edge, if so save edge
          // at a block edge -- possible need to adjust interblock
          string edgeDir;
          if (this->AtEdge(indB[0], indB[1], indB[2], true, edgeDir)) {
            int dir1, dir2;
            if (inter.Direction1First() == "i") {
              dir1 = 0;  // direction 1 is i
              dir2 = 1;  // direction 2 is j
            } else if (inter.Direction1First() == "j") {
              dir1 = 1;  // direction 1 is j
              dir2 = 2;  // direction 2 is k
            } else {
              dir1 = 2;  // direction 1 is k
              dir2 = 0;  // direction 2 is i
            }

            // find out edge direction
            // edge direction matches interblock direction 1
            if (edgeDir == inter.Direction1First()) {
              // adjust edge on lower dir2 side
              if (indB[dir2] < inter.Dir2StartFirst() + numGhosts_) {
                adjEdge[2] = true;
              } else {  // adjust edge on upper dir2 side
                adjEdge[3] = true;
              }
            // edge direction matches interblock direction 2
            } else if (edgeDir == inter.Direction2First()) {
              // adjust edge on lower dir1 side
              if (indB[dir1] < inter.Dir1StartFirst() + numGhosts_) {
                adjEdge[0] = true;
              } else {  // adjust edge on upper dir1 side
                adjEdge[1] = true;
              }
            } else {
              cerr << "ERROR: Error in procBlock::PutStateSlice(). Ghost cell "
                      "edge direction does not match interblock direction 1 or "
                      "2." << endl;
              exit(0);
            }
          }

        // volume is not 0, ok to overwrite variables
        } else {
          // swap cell data
          vol_(indB[0], indB[1], indB[2]) = slice.Vol(indS[0], indS[1],
                                                      indS[2]);
          center_(indB[0], indB[1], indB[2]) = slice.Center(indS[0], indS[1],
                                                            indS[2]);

          //-------------------------------------------------------------------
          // swap face data
          // both patches i, i to i, j to j, k to k
          if (inter.Direction3First() == "i" &&
              inter.Direction3Second() == "i") {
            // swap face data for direction 3
            fCenterI_(indB[0], indB[1], indB[2]) =
                slice.FCenterI(indS[0], indS[1], indS[2]);
            fAreaI_(indB[0], indB[1], indB[2]) = aFac3 *
                slice.FAreaI(indS[0], indS[1], indS[2]);

            if (l3 == (d3 - 1)) {  // at end of direction 3 line
              fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                  slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
              fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac3 *
                  slice.FAreaI(indS[0] + 1, indS[1], indS[2]);
            }

            // swap face data for direction 1
            if (aFac1 == 1.0) {
              fCenterJ_(indB[0], indB[1], indB[2]) =
                  slice.FCenterJ(indS[0], indS[1], indS[2]);
              fAreaJ_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaJ(indS[0], indS[1], indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                    slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
                fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac1 *
                    slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterJ_(indB[0], indB[1], indB[2]) =
                  slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
              fAreaJ_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                    slice.FCenterJ(indS[0], indS[1], indS[2]);
                fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac1 *
                    slice.FAreaJ(indS[0], indS[1], indS[2]);
              }
            }

            // swap face data for direction 2
            if (aFac2 == 1.0) {
              fCenterK_(indB[0], indB[1], indB[2]) =
                  slice.FCenterK(indS[0], indS[1], indS[2]);
              fAreaK_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaK(indS[0], indS[1], indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterK_(indB[0], indB[1], indB[2] + 1) =
                    slice.FCenterK(indS[0], indS[1], indS[2] + 1);
                fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac2 *
                    slice.FAreaK(indS[0], indS[1], indS[2] + 1);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterK_(indB[0], indB[1], indB[2]) =
                  slice.FCenterK(indS[0], indS[1], indS[2] + 1);
              fAreaK_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaK(indS[0], indS[1], indS[2] + 1);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterK_(indB[0], indB[1], indB[2] + 1) =
                    slice.FCenterK(indS[0], indS[1], indS[2]);
                fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac2 *
                    slice.FAreaK(indS[0], indS[1], indS[2]);
              }
            }

          //----------------------------------------------------------------
          // both patches j, j to j, k to k, i to i
          } else if (inter.Direction3First() == "j" &&
                     inter.Direction3Second() == "j") {
            // swap face data for direction 3
            fCenterJ_(indB[0], indB[1], indB[2]) =
                slice.FCenterJ(indS[0], indS[1], indS[2]);
            fAreaJ_(indB[0], indB[1], indB[2]) = aFac3 *
                slice.FAreaJ(indS[0], indS[1], indS[2]);

            if (l3 == (d3 - 1)) {  // at end of direction 3 line
              fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                  slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
              fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac3 *
                  slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);
            }

            // swap face data for direction 1
            if (aFac1 == 1.0) {
              fCenterK_(indB[0], indB[1], indB[2]) =
                  slice.FCenterK(indS[0], indS[1], indS[2]);
              fAreaK_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaK(indS[0], indS[1], indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterK_(indB[0], indB[1], indB[2] + 1) =
                    slice.FCenterK(indS[0], indS[1], indS[2] + 1);
                fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac1 *
                    slice.FAreaK(indS[0], indS[1], indS[2] + 1);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterK_(indB[0], indB[1], indB[2]) =
                  slice.FCenterK(indS[0], indS[1], indS[2] + 1);
              fAreaK_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaK(indS[0], indS[1], indS[2] + 1);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterK_(indB[0], indB[1], indB[2] + 1) =
                    slice.FCenterK(indS[0], indS[1], indS[2]);
                fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac1 *
                    slice.FAreaK(indS[0], indS[1], indS[2]);
              }
            }

            // swap face data for direction 2
            if (aFac2 == 1.0) {
              fCenterI_(indB[0], indB[1], indB[2]) =
                  slice.FCenterI(indS[0], indS[1], indS[2]);
              fAreaI_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaI(indS[0], indS[1], indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                    slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
                fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac2 *
                    slice.FAreaI(indS[0] + 1, indS[1], indS[2]);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterI_(indB[0], indB[1], indB[2]) =
                  slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
              fAreaI_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaI(indS[0] + 1, indS[1], indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                    slice.FCenterI(indS[0], indS[1], indS[2]);
                fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac2 *
                    slice.FAreaI(indS[0], indS[1], indS[2]);
              }
            }

          //-------------------------------------------------------------
          // both patches k, k to k, i to i, j to j
          } else if (inter.Direction3First() == "k" &&
                     inter.Direction3Second() == "k") {
            // swap face data for direction 3
            fCenterK_(indB[0], indB[1], indB[2]) =
                slice.FCenterK(indS[0], indS[1], indS[2]);
            fAreaK_(indB[0], indB[1], indB[2]) = aFac3 *
                slice.FAreaK(indS[0], indS[1], indS[2]);

            if (l3 == (d3 - 1)) {  // at end of direction 3 line
              fCenterK_(indB[0], indB[1], indB[2] + 1) =
                  slice.FCenterK(indS[0], indS[1], indS[2] + 1);
              fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac3 *
                  slice.FAreaK(indS[0], indS[1], indS[2] + 1);
            }

            // swap face data for direction 1
            if (aFac1 == 1.0) {
              fCenterI_(indB[0], indB[1], indB[2]) =
                  slice.FCenterI(indS[0], indS[1], indS[2]);
              fAreaI_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaI(indS[0], indS[1], indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                    slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
                fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac1 *
                    slice.FAreaI(indS[0] + 1, indS[1], indS[2]);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterI_(indB[0], indB[1], indB[2]) =
                  slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
              fAreaI_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaI(indS[0] + 1, indS[1], indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                    slice.FCenterI(indS[0], indS[1], indS[2]);
                fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac1 *
                    slice.FAreaI(indS[0], indS[1], indS[2]);
              }
            }

            // swap face data for direction 2
            if (aFac2 == 1.0) {
              fCenterJ_(indB[0], indB[1], indB[2]) =
                  slice.FCenterJ(indS[0], indS[1], indS[2]);
              fAreaJ_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaJ(indS[0], indS[1], indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                    slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
                fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac2 *
                    slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterJ_(indB[0], indB[1], indB[2]) =
                  slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
              fAreaJ_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                    slice.FCenterJ(indS[0], indS[1], indS[2]);
                fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac2 *
                    slice.FAreaJ(indS[0], indS[1], indS[2]);
              }
            }

          //---------------------------------------------------------------
          // patches are i/j  - i to j, j to k, k to i
          } else if (inter.Direction3First() == "i" &&
                     inter.Direction3Second() == "j") {
            // swap face data for direction 3
            fCenterI_(indB[0], indB[1], indB[2]) =
                slice.FCenterJ(indS[0], indS[1], indS[2]);
            fAreaI_(indB[0], indB[1], indB[2]) = aFac3 *
                slice.FAreaJ(indS[0], indS[1], indS[2]);

            if (l3 == (d3 - 1)) {  // at end of direction 3 line
              fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                  slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
              fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac3 *
                  slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);
            }

            // swap face data for direction 1
            if (aFac1 == 1.0) {
              fCenterJ_(indB[0], indB[1], indB[2]) =
                  slice.FCenterK(indS[0], indS[1], indS[2]);
              fAreaJ_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaK(indS[0], indS[1], indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                    slice.FCenterK(indS[0], indS[1], indS[2] + 1);
                fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac1 *
                    slice.FAreaK(indS[0], indS[1], indS[2] + 1);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterJ_(indB[0], indB[1], indB[2]) =
                  slice.FCenterK(indS[0], indS[1], indS[2] + 1);
              fAreaJ_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaK(indS[0], indS[1], indS[2] + 1);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                    slice.FCenterK(indS[0], indS[1], indS[2]);
                fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac1 *
                    slice.FAreaK(indS[0], indS[1], indS[2]);
              }
            }

            // swap face data for direction 2
            if (aFac2 == 1.0) {
              fCenterK_(indB[0], indB[1], indB[2]) =
                  slice.FCenterI(indS[0], indS[1], indS[2]);
              fAreaK_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaI(indS[0], indS[1], indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterK_(indB[0], indB[1], indB[2] + 1) =
                    slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
                fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac2 *
                    slice.FAreaI(indS[0] + 1, indS[1], indS[2]);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterK_(indB[0], indB[1], indB[2]) =
                  slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
              fAreaK_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaI(indS[0] + 1, indS[1], indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterK_(indB[0], indB[1], indB[2] + 1) =
                    slice.FCenterI(indS[0], indS[1], indS[2]);
                fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac2 *
                    slice.FAreaI(indS[0], indS[1], indS[2]);
              }
            }

          //------------------------------------------------------------------
          // patches are i/k  - i to k, j to i, k to j
          } else if (inter.Direction3First() == "i" &&
                     inter.Direction3Second() == "k") {
            // swap face data for direction 3
            fCenterI_(indB[0], indB[1], indB[2]) =
                slice.FCenterK(indS[0], indS[1], indS[2]);
            fAreaI_(indB[0], indB[1], indB[2]) = aFac3 *
                slice.FAreaK(indS[0], indS[1], indS[2]);

            if (l3 == (d3 - 1)) {  // at end of direction 3 line
              fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                  slice.FCenterK(indS[0], indS[1], indS[2] + 1);
              fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac3 *
                  slice.FAreaK(indS[0], indS[1], indS[2] + 1);
            }

            // swap face data for direction 1
            if (aFac1 == 1.0) {
              fCenterJ_(indB[0], indB[1], indB[2]) =
                  slice.FCenterI(indS[0], indS[1], indS[2]);
              fAreaJ_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaI(indS[0], indS[1], indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                    slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
                fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac1 *
                    slice.FAreaI(indS[0] + 1, indS[1], indS[2]);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterJ_(indB[0], indB[1], indB[2]) =
                  slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
              fAreaJ_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaI(indS[0] + 1, indS[1], indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                    slice.FCenterI(indS[0], indS[1], indS[2]);
                fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac1 *
                    slice.FAreaI(indS[0], indS[1], indS[2]);
              }
            }

            // swap face data for direction 2
            if (aFac2 == 1.0) {
              fCenterK_(indB[0], indB[1], indB[2]) =
                  slice.FCenterJ(indS[0], indS[1], indS[2]);
              fAreaK_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaJ(indS[0], indS[1], indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterK_(indB[0], indB[1], indB[2] + 1) =
                    slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
                fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac2 *
                    slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterK_(indB[0], indB[1], indB[2]) =
                  slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
              fAreaK_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterK_(indB[0], indB[1], indB[2] + 1) =
                    slice.FCenterJ(indS[0], indS[1], indS[2]);
                fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac2 *
                    slice.FAreaJ(indS[0], indS[1], indS[2]);
              }
            }

          //--------------------------------------------------------------
          // patches are j/i, j to i, k to j, i to k
          } else if (inter.Direction3First() == "j" &&
                     inter.Direction3Second() == "i") {
            // swap face data for direction 3
            fCenterJ_(indB[0], indB[1], indB[2]) =
                slice.FCenterI(indS[0], indS[1], indS[2]);
            fAreaJ_(indB[0], indB[1], indB[2]) = aFac3 *
                slice.FAreaI(indS[0], indS[1], indS[2]);

            if (l3 == (d3 - 1)) {  // at end of direction 3 line
              fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                  slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
              fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac3 *
                  slice.FAreaI(indS[0] + 1, indS[1], indS[2]);
            }

            // swap face data for direction 1
            if (aFac1 == 1.0) {
              fCenterK_(indB[0], indB[1], indB[2]) =
                  slice.FCenterJ(indS[0], indS[1], indS[2]);
              fAreaK_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaJ(indS[0], indS[1], indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterK_(indB[0], indB[1], indB[2] + 1) =
                    slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
                fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac1 *
                    slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterK_(indB[0], indB[1], indB[2]) =
                  slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
              fAreaK_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterK_(indB[0], indB[1], indB[2] + 1) =
                    slice.FCenterJ(indS[0], indS[1], indS[2]);
                fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac1 *
                    slice.FAreaJ(indS[0], indS[1], indS[2]);
              }
            }

            // swap face data for direction 2
            if (aFac2 == 1.0) {
              fCenterI_(indB[0], indB[1], indB[2]) =
                  slice.FCenterK(indS[0], indS[1], indS[2]);
              fAreaI_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaK(indS[0], indS[1], indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                    slice.FCenterK(indS[0], indS[1], indS[2] + 1);
                fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac2 *
                    slice.FAreaK(indS[0], indS[1], indS[2] + 1);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterI_(indB[0], indB[1], indB[2]) =
                  slice.FCenterK(indS[0], indS[1], indS[2] + 1);
              fAreaI_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaK(indS[0], indS[1], indS[2] + 1);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                    slice.FCenterK(indS[0], indS[1], indS[2]);
                fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac2 *
                    slice.FAreaK(indS[0], indS[1], indS[2]);
              }
            }


          //-----------------------------------------------------------------
          // patches are j/k, j to k, k to i, i to j
          } else if (inter.Direction3First() > "j" &&
                     inter.Direction3Second() == "k") {
            // swap face data for direction 3
            fCenterJ_(indB[0], indB[1], indB[2]) =
                slice.FCenterK(indS[0], indS[1], indS[2]);
            fAreaJ_(indB[0], indB[1], indB[2]) = aFac3 *
                slice.FAreaK(indS[0], indS[1], indS[2]);

            if (l3 == (d3 - 1)) {  // at end of direction 3 line
              fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                  slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
              fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac3 *
                  slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);
            }

            // swap face data for direction 1
            if (aFac1 == 1.0) {
              fCenterK_(indB[0], indB[1], indB[2]) =
                  slice.FCenterI(indS[0], indS[1], indS[2]);
              fAreaK_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaI(indS[0], indS[1], indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterK_(indB[0], indB[1], indB[2] + 1) =
                    slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
                fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac1 *
                    slice.FAreaI(indS[0] + 1, indS[1], indS[2]);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterK_(indB[0], indB[1], indB[2]) =
                  slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
              fAreaK_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaI(indS[0] + 1, indS[1], indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterK_(indB[0], indB[1], indB[2] + 1) =
                    slice.FCenterI(indS[0], indS[1], indS[2]);
                fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac1 *
                    slice.FAreaI(indS[0], indS[1], indS[2]);
              }
            }

            // swap face data for direction 2
            if (aFac2 == 1.0) {
              fCenterI_(indB[0], indB[1], indB[2]) =
                  slice.FCenterJ(indS[0], indS[1], indS[2]);
              fAreaI_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaJ(indS[0], indS[1], indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                    slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
                fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac2 *
                    slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterI_(indB[0], indB[1], indB[2]) =
                  slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
              fAreaI_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                    slice.FCenterJ(indS[0], indS[1], indS[2]);
                fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac2 *
                    slice.FAreaJ(indS[0], indS[1], indS[2]);
              }
            }

          //------------------------------------------------------------------
          // patches are k/i, k to i, i to j, j to k
          } else if (inter.Direction3First() == "k" &&
                     inter.Direction3Second() == "i") {
            // swap face data for direction 3
            fCenterK_(indB[0], indB[1], indB[2]) =
                slice.FCenterI(indS[0], indS[1], indS[2]);
            fAreaK_(indB[0], indB[1], indB[2]) = aFac3 *
                slice.FAreaI(indS[0], indS[1], indS[2]);

            if (l3 == (d3 - 1)) {  // at end of direction 3 line
              fCenterK_(indB[0], indB[1], indB[2] + 1) =
                  slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
              fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac3 *
                  slice.FAreaI(indS[0] + 1, indS[1], indS[2]);
            }

            // swap face data for direction 1
            if (aFac1 == 1.0) {
              fCenterI_(indB[0], indB[1], indB[2]) =
                  slice.FCenterJ(indS[0], indS[1], indS[2]);
              fAreaI_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaJ(indS[0], indS[1], indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                    slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
                fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac1 *
                    slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterI_(indB[0], indB[1], indB[2]) =
                  slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
              fAreaI_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                    slice.FCenterJ(indS[0], indS[1], indS[2]);
                fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac1 *
                    slice.FAreaJ(indS[0], indS[1], indS[2]);
              }
            }

            // swap face data for direction 2
            if (aFac2 == 1.0) {
              fCenterJ_(indB[0], indB[1], indB[2]) =
                  slice.FCenterK(indS[0], indS[1], indS[2]);
              fAreaJ_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaK(indS[0], indS[1], indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                    slice.FCenterK(indS[0], indS[1], indS[2] + 1);
                fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac2 *
                    slice.FAreaK(indS[0], indS[1], indS[2] + 1);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterJ_(indB[0], indB[1], indB[2]) =
                  slice.FCenterK(indS[0], indS[1], indS[2] + 1);
              fAreaJ_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaK(indS[0], indS[1], indS[2] + 1);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                    slice.FCenterK(indS[0], indS[1], indS[2]);
                fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac2 *
                    slice.FAreaK(indS[0], indS[1], indS[2]);
              }
            }

          //-------------------------------------------------------------------
          // patches are k/j, k to j, i to k, j to i
          } else if (inter.Direction3First() == "k" &&
                     inter.Direction3Second() == "j") {
            // swap face data for direction 3
            fCenterK_(indB[0], indB[1], indB[2]) =
                slice.FCenterJ(indS[0], indS[1], indS[2]);
            fAreaK_(indB[0], indB[1], indB[2]) = aFac3 *
                slice.FAreaJ(indS[0], indS[1], indS[2]);

            if (l3 == (d3 - 1)) {  // at end of direction 3 line
              fCenterK_(indB[0], indB[1], indB[2] + 1) =
                  slice.FCenterJ(indS[0], indS[1] + 1, indS[2]);
              fAreaK_(indB[0], indB[1], indB[2] + 1) = aFac3 *
                  slice.FAreaJ(indS[0], indS[1] + 1, indS[2]);
            }

            // swap face data for direction 1
            if (aFac1 == 1.0) {
              fCenterI_(indB[0], indB[1], indB[2]) =
                  slice.FCenterK(indS[0], indS[1], indS[2]);
              fAreaI_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaK(indS[0], indS[1], indS[2]);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                    slice.FCenterK(indS[0], indS[1], indS[2] + 1);
                fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac1 *
                    slice.FAreaK(indS[0], indS[1], indS[2] + 1);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterI_(indB[0], indB[1], indB[2]) =
                  slice.FCenterK(indS[0], indS[1], indS[2] + 1);
              fAreaI_(indB[0], indB[1], indB[2]) = aFac1 *
                  slice.FAreaK(indS[0], indS[1], indS[2] + 1);

              // at end of direction 1 line
              if (l1 == (inter.Dir1EndFirst() - inter.Dir1StartFirst() - 1)) {
                fCenterI_(indB[0] + 1, indB[1], indB[2]) =
                    slice.FCenterK(indS[0], indS[1], indS[2]);
                fAreaI_(indB[0] + 1, indB[1], indB[2]) = aFac1 *
                    slice.FAreaK(indS[0], indS[1], indS[2]);
              }
            }

            // swap face data for direction 2
            if (aFac2 == 1.0) {
              fCenterJ_(indB[0], indB[1], indB[2]) =
                  slice.FCenterI(indS[0], indS[1], indS[2]);
              fAreaJ_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaI(indS[0], indS[1], indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                    slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
                fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac2 *
                    slice.FAreaI(indS[0] + 1, indS[1], indS[2]);
              }
            // if direction is reversed, upper/lower faces need to be swapped
            } else {
              fCenterJ_(indB[0], indB[1], indB[2]) =
                  slice.FCenterI(indS[0] + 1, indS[1], indS[2]);
              fAreaJ_(indB[0], indB[1], indB[2]) = aFac2 *
                  slice.FAreaI(indS[0] + 1, indS[1], indS[2]);

              // at end of direction 2 line
              if (l2 == (inter.Dir2EndFirst() - inter.Dir2StartFirst() - 1)) {
                fCenterJ_(indB[0], indB[1] + 1, indB[2]) =
                    slice.FCenterI(indS[0], indS[1], indS[2]);
                fAreaJ_(indB[0], indB[1] + 1, indB[2]) = aFac2 *
                    slice.FAreaI(indS[0], indS[1], indS[2]);
              }
            }

          //-------------------------------------------------------------------
          } else {
            cerr << "ERROR: Error in procBlock::PutGeomSlice(). Unable to swap "
                    "face quantities because behavior for interface with "
                    "boundary pair " << inter.BoundaryFirst() << ", "
                 << inter.BoundarySecond() << " is not defined." << endl;
            exit(0);
          }
        }
      }
    }
  }

  return adjEdge;
}

/* Member function to overwrite a section of a procBlock's states with a
stateSlice. The function uses the orientation supplied in the interblock to
orient the stateSlice relative to the procBlock. It assumes that the procBlock
is listed first, and the stateSlice second in the interblock data structure.
*/
void procBlock::PutStateSlice(const stateSlice &slice, const interblock &inter,
                              const int &d3, const int &numG) {
  // slice -- geomSlice to insert int procBlock
  // inter -- interblock data structure defining the patches and their
  // orientation
  // d3 -- distance of direction normal to patch to insert
  // numG -- number of ghost cells

  // check that number of cells to insert matches
  int blkCell = (inter.Dir1EndFirst() - inter.Dir1StartFirst()) *
                (inter.Dir2EndFirst() - inter.Dir2StartFirst()) * d3;
  if (blkCell != slice.NumCells()) {
    cerr << "ERROR: Error in procBlock::PutStateSlice(). Number of cells being "
            "inserted does not match designated space to insert to." << endl;
    cerr << "Direction 1, 2, 3 of procBlock: "
         << inter.Dir1EndFirst() - inter.Dir1StartFirst() << ", "
         << inter.Dir2EndFirst() - inter.Dir2StartFirst() << ", " << d3 << endl;
    cerr << "Direction I, J, K of geomSlice: " << slice.NumI() << ", "
         << slice.NumJ() << ", " << slice.NumK() << endl;
    exit(0);
  }

  // adjust insertion indices if patch borders another interblock on the same
  // surface of the block
  int adjS1 = (inter.Dir1StartInterBorderFirst()) ? numG : 0;
  int adjE1 = (inter.Dir1EndInterBorderFirst()) ? numG : 0;
  int adjS2 = (inter.Dir2StartInterBorderFirst()) ? numG : 0;
  int adjE2 = (inter.Dir2EndInterBorderFirst()) ? numG : 0;

  // loop over cells to insert
  for (int l3 = 0; l3 < d3; l3++) {
    for (int l2 = adjS2;
         l2 < (inter.Dir2EndFirst() - inter.Dir2StartFirst() - adjE2); l2++) {
      for (int l1 = adjS1;
           l1 < (inter.Dir1EndFirst() - inter.Dir1StartFirst() - adjE1); l1++) {
        // get block and slice indices
        vector3d<int> indB = GetSwapLoc(l1, l2, l3, inter, true);
        vector3d<int> indS = GetSwapLoc(l1, l2, l3, inter, false);

        // swap cell data
        state_(indB[0], indB[1], indB[2]) =
            slice.State(indS[0], indS[1], indS[2]);
      }
    }
  }
}

/*Member function to pack and send procBlock geometry data to appropriate
 * processor. */
void procBlock::PackSendGeomMPI(const MPI_Datatype &MPI_cellData,
                                const MPI_Datatype &MPI_vec3d,
                                const MPI_Datatype &MPI_vec3dMag) const {
  // MPI_cellData -- MPI data type for cell data
  // MPI_vec3d -- MPI data type for a vector3d
  // MPI_vec3dMag -- MPI data type for a unitVect3dMag

  // determine size of buffer to send
  int sendBufSize = 0;
  int tempSize = 0;
  // adding 3 more ints for block dimensions
  MPI_Pack_size(8, MPI_INT, MPI_COMM_WORLD,
                &tempSize);  // add size for ints in class procBlock
  sendBufSize += tempSize;
  MPI_Pack_size(state_.Size(), MPI_cellData, MPI_COMM_WORLD,
                &tempSize);  // add size for states
  sendBufSize += tempSize;
  MPI_Pack_size(center_.Size(), MPI_vec3d, MPI_COMM_WORLD,
                &tempSize);  // add size for cell centers
  sendBufSize += tempSize;
  MPI_Pack_size(fAreaI_.Size(), MPI_vec3dMag, MPI_COMM_WORLD,
                &tempSize);  // add size for face area I
  sendBufSize += tempSize;
  MPI_Pack_size(fAreaJ_.Size(), MPI_vec3dMag, MPI_COMM_WORLD,
                &tempSize);  // add size for face area J
  sendBufSize += tempSize;
  MPI_Pack_size(fAreaK_.Size(), MPI_vec3dMag, MPI_COMM_WORLD,
                &tempSize);  // add size for face area K
  sendBufSize += tempSize;
  MPI_Pack_size(fCenterI_.Size(), MPI_vec3d, MPI_COMM_WORLD,
                &tempSize);  // add size for face center_ I
  sendBufSize += tempSize;
  MPI_Pack_size(fCenterJ_.Size(), MPI_vec3d, MPI_COMM_WORLD,
                &tempSize);  // add size for face center_ J
  sendBufSize += tempSize;
  MPI_Pack_size(fCenterK_.Size(), MPI_vec3d, MPI_COMM_WORLD,
                &tempSize);  // add size for face center_ K
  sendBufSize += tempSize;
  MPI_Pack_size(vol_.Size(), MPI_DOUBLE, MPI_COMM_WORLD,
                &tempSize);  // add size for volumes
  sendBufSize += tempSize;
  MPI_Pack_size(3, MPI_INT, MPI_COMM_WORLD,
                &tempSize);  // add size for number of bc surfaces
  sendBufSize += tempSize;
  // 8x because iMin, iMax, jMin, jMax, kMin, kMax, tags, string sizes
  MPI_Pack_size(bc_.NumSurfaces() * 8, MPI_INT, MPI_COMM_WORLD,
                &tempSize);  // add size for BCs
  sendBufSize += tempSize;

  int stringSize = 0;
  for (int jj = 0; jj < bc_.NumSurfaces(); jj++) {
    MPI_Pack_size(
        bc_.GetBCTypes(jj).size() + 1, MPI_CHAR, MPI_COMM_WORLD,
        &tempSize);  // add size for bc_ types (+1 for c_str end character)
    stringSize += tempSize;
  }
  sendBufSize += stringSize;

  // allocate buffer to pack data into
  char *sendBuffer = new char[sendBufSize];

  int numI = this->NumI();
  int numJ = this->NumJ();
  int numK = this->NumK();

  // pack data to send into buffer
  int position = 0;
  // int and vector data
  MPI_Pack(&numI, 1, MPI_INT, sendBuffer, sendBufSize, &position,
           MPI_COMM_WORLD);
  MPI_Pack(&numJ, 1, MPI_INT, sendBuffer, sendBufSize, &position,
           MPI_COMM_WORLD);
  MPI_Pack(&numK, 1, MPI_INT, sendBuffer, sendBufSize, &position,
           MPI_COMM_WORLD);
  MPI_Pack(&numGhosts_, 1, MPI_INT, sendBuffer, sendBufSize, &position,
           MPI_COMM_WORLD);
  MPI_Pack(&parBlock_, 1, MPI_INT, sendBuffer, sendBufSize, &position,
           MPI_COMM_WORLD);
  MPI_Pack(&rank_, 1, MPI_INT, sendBuffer, sendBufSize, &position,
           MPI_COMM_WORLD);
  MPI_Pack(&localPos_, 1, MPI_INT, sendBuffer, sendBufSize, &position,
           MPI_COMM_WORLD);
  MPI_Pack(&globalPos_, 1, MPI_INT, sendBuffer, sendBufSize, &position,
           MPI_COMM_WORLD);
  MPI_Pack(&state_(0, 0, 0), state_.Size(), MPI_cellData, sendBuffer,
           sendBufSize, &position, MPI_COMM_WORLD);
  MPI_Pack(&center_(0, 0, 0), center_.Size(), MPI_vec3d, sendBuffer,
           sendBufSize, &position, MPI_COMM_WORLD);
  MPI_Pack(&fAreaI_(0, 0, 0), fAreaI_.Size(), MPI_vec3dMag,
           sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
  MPI_Pack(&fAreaJ_(0, 0, 0), fAreaJ_.Size(), MPI_vec3dMag,
           sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
  MPI_Pack(&fAreaK_(0, 0, 0), fAreaK_.Size(), MPI_vec3dMag,
           sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
  MPI_Pack(&fCenterI_(0, 0, 0), fCenterI_.Size(), MPI_vec3d,
           sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
  MPI_Pack(&fCenterJ_(0, 0, 0), fCenterJ_.Size(), MPI_vec3d,
           sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
  MPI_Pack(&fCenterK_(0, 0, 0), fCenterK_.Size(), MPI_vec3d,
           sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
  MPI_Pack(&vol_(0, 0, 0), vol_.Size(), MPI_DOUBLE, sendBuffer,
           sendBufSize, &position, MPI_COMM_WORLD);

  // pack boundary condition data
  bc_.PackBC(sendBuffer, sendBufSize, position);

  // send buffer to appropriate processor
  MPI_Send(sendBuffer, sendBufSize, MPI_PACKED, rank_, 2,
           MPI_COMM_WORLD);

  delete[] sendBuffer;  // deallocate buffer
}

void procBlock::RecvUnpackGeomMPI(const MPI_Datatype &MPI_cellData,
                                  const MPI_Datatype &MPI_vec3d,
                                  const MPI_Datatype &MPI_vec3dMag) {
  // MPI_cellData -- MPI data type for cell data
  // MPI_vec3d -- MPI data type for a vector3d
  // MPI_vec3dMag -- MPI data type for a unitVect3dMag

  MPI_Status status;  // allocate MPI_Status structure

  // probe message to get correct data size
  int recvBufSize = 0;
  MPI_Probe(ROOTP, 2, MPI_COMM_WORLD, &status);
  MPI_Get_count(&status, MPI_CHAR, &recvBufSize);  // use MPI_CHAR because
                                                   // sending buffer was
                                                   // allocated with chars

  char *recvBuffer = new char[recvBufSize];  // allocate buffer of correct size

  // receive message from ROOT
  MPI_Recv(recvBuffer, recvBufSize, MPI_PACKED, ROOTP, 2, MPI_COMM_WORLD,
           &status);

  int numI, numJ, numK;
  // unpack procBlock INTs
  int position = 0;
  MPI_Unpack(recvBuffer, recvBufSize, &position, &numI, 1,
             MPI_INT, MPI_COMM_WORLD);
  MPI_Unpack(recvBuffer, recvBufSize, &position, &numJ, 1,
             MPI_INT, MPI_COMM_WORLD);
  MPI_Unpack(recvBuffer, recvBufSize, &position, &numK, 1,
             MPI_INT, MPI_COMM_WORLD);
  MPI_Unpack(recvBuffer, recvBufSize, &position, &numGhosts_, 1,
             MPI_INT, MPI_COMM_WORLD);
  MPI_Unpack(recvBuffer, recvBufSize, &position, &parBlock_, 1, MPI_INT,
             MPI_COMM_WORLD);
  MPI_Unpack(recvBuffer, recvBufSize, &position, &rank_, 1, MPI_INT,
             MPI_COMM_WORLD);
  MPI_Unpack(recvBuffer, recvBufSize, &position, &localPos_, 1, MPI_INT,
             MPI_COMM_WORLD);
  MPI_Unpack(recvBuffer, recvBufSize, &position, &globalPos_, 1,
             MPI_INT, MPI_COMM_WORLD);

  // clean and resize the vectors in the class to
  this->CleanResizeVecs(numI, numJ, numK);

  // unpack vector data into allocated vectors
  MPI_Unpack(recvBuffer, recvBufSize, &position, &state_(0, 0, 0),
             state_.Size(), MPI_cellData,
             MPI_COMM_WORLD);  // unpack states
  MPI_Unpack(recvBuffer, recvBufSize, &position, &center_(0, 0, 0),
             center_.Size(), MPI_vec3d,
             MPI_COMM_WORLD);  // unpack cell centers
  MPI_Unpack(recvBuffer, recvBufSize, &position, &fAreaI_(0, 0, 0),
             fAreaI_.Size(), MPI_vec3dMag,
             MPI_COMM_WORLD);  // unpack face area I
  MPI_Unpack(recvBuffer, recvBufSize, &position, &fAreaJ_(0, 0, 0),
             fAreaJ_.Size(), MPI_vec3dMag,
             MPI_COMM_WORLD);  // unpack face area J
  MPI_Unpack(recvBuffer, recvBufSize, &position, &fAreaK_(0, 0, 0),
             fAreaK_.Size(), MPI_vec3dMag,
             MPI_COMM_WORLD);  // unpack face area K
  MPI_Unpack(recvBuffer, recvBufSize, &position, &fCenterI_(0, 0, 0),
             fCenterI_.Size(), MPI_vec3d,
             MPI_COMM_WORLD);  // unpack face center_ I
  MPI_Unpack(recvBuffer, recvBufSize, &position, &fCenterJ_(0, 0, 0),
             fCenterJ_.Size(), MPI_vec3d,
             MPI_COMM_WORLD);  // unpack face center_ J
  MPI_Unpack(recvBuffer, recvBufSize, &position, &fCenterK_(0, 0, 0),
             fCenterK_.Size(), MPI_vec3d,
             MPI_COMM_WORLD);  // unpack face center_ K
  MPI_Unpack(recvBuffer, recvBufSize, &position, &vol_(0, 0, 0),
             vol_.Size(), MPI_DOUBLE,
             MPI_COMM_WORLD);  // unpack volumes

  // unpack boundary conditions
  bc_.UnpackBC(recvBuffer, recvBufSize, position);

  delete[] recvBuffer;  // deallocate receiving buffer
}

/*Member function to zero and resize the vectors in a procBlock to their
 * appropriate size given the i, j, and k dimensions.*/
void procBlock::CleanResizeVecs(const int &numI, const int &numJ,
                                const int &numK) {
  // numI -- i-dimension to resize to (no ghosts)
  // numJ -- j-dimension to resize to (no ghosts)
  // numK -- k-dimension to resize to (no ghosts)

  // indices for variables with ghost cells
  int ig = numI + numGhosts_;
  int jg = numJ + numGhosts_;
  int kg = numK + numGhosts_;

  state_.ClearResize(ig, jg, kg);
  center_.ClearResize(ig, jg, kg);
  vol_.ClearResize(ig, jg, kg);

  fCenterI_.ClearResize(ig + 1, jg, kg);
  fAreaI_.ClearResize(ig + 1, jg, kg);

  fCenterJ_.ClearResize(ig, jg + 1, kg);
  fAreaJ_.ClearResize(ig, jg + 1, kg);

  fCenterK_.ClearResize(ig, jg, kg + 1);
  fAreaK_.ClearResize(ig, jg, kg + 1);

  residual_.ClearResize(numI, numJ, numK);
  avgWaveSpeed_.ClearResize(numI, numJ, numK);
  dt_.ClearResize(numI, numJ, numK);
  wallDist_.ClearResize(numI, numJ, numK, DEFAULTWALLDIST);
}

/*Member function to receive and unpack procBlock state data. This is used to
 * gather the solution on the ROOT processor to write out the solution. */
void procBlock::RecvUnpackSolMPI(const MPI_Datatype &MPI_cellData) {
  // MPI_cellData -- MPI data type for cell data

  MPI_Status status;  // allocate MPI_Status structure

  // probe message to get correct data size
  int recvBufSize = 0;
  MPI_Probe(rank_, globalPos_, MPI_COMM_WORLD,
            &status);  // global position used as tag because each block has a
                       // unique one
  MPI_Get_count(&status, MPI_CHAR, &recvBufSize);  // use MPI_CHAR because
                                                   // sending buffer was
                                                   // allocated with chars

  char *recvBuffer = new char[recvBufSize];  // allocate buffer of correct size

  // receive message from non-ROOT
  MPI_Recv(recvBuffer, recvBufSize, MPI_PACKED, rank_,
           globalPos_, MPI_COMM_WORLD, &status);

  // unpack vector data into allocated vectors
  int position = 0;
  MPI_Unpack(recvBuffer, recvBufSize, &position, &state_(0, 0, 0),
             state_.Size(), MPI_cellData,
             MPI_COMM_WORLD);  // unpack states
  MPI_Unpack(recvBuffer, recvBufSize, &position, &residual_(0, 0, 0),
             residual_.Size(), MPI_cellData,
             MPI_COMM_WORLD);  // unpack residuals
  MPI_Unpack(recvBuffer, recvBufSize, &position, &dt_(0, 0, 0),
             dt_.Size(), MPI_DOUBLE,
             MPI_COMM_WORLD);  // unpack time steps
  MPI_Unpack(recvBuffer, recvBufSize, &position, &wallDist_(0, 0, 0),
             wallDist_.Size(), MPI_DOUBLE,
             MPI_COMM_WORLD);  // unpack wall distance
  MPI_Unpack(recvBuffer, recvBufSize, &position, &avgWaveSpeed_(0, 0, 0),
             avgWaveSpeed_.Size(), MPI_DOUBLE,
             MPI_COMM_WORLD);  // unpack average wave speeds

  delete[] recvBuffer;  // deallocate receiving buffer
}

/*Member function to pack and send procBlock state data to the ROOT proecessor.
 * This is used to gather the solution on the ROOT processor to write out the
 * solution. */
void procBlock::PackSendSolMPI(const MPI_Datatype &MPI_cellData) const {
  // MPI_cellData -- MPI data type for cell data

  // determine size of buffer to send
  int sendBufSize = 0;
  int tempSize = 0;
  MPI_Pack_size(state_.Size(), MPI_cellData, MPI_COMM_WORLD,
                &tempSize);  // add size for states
  sendBufSize += tempSize;
  MPI_Pack_size(residual_.Size(), MPI_cellData, MPI_COMM_WORLD,
                &tempSize);  // add size for residuals
  sendBufSize += tempSize;
  MPI_Pack_size(dt_.Size(), MPI_DOUBLE, MPI_COMM_WORLD,
                &tempSize);  // add size for time steps
  sendBufSize += tempSize;
  MPI_Pack_size(wallDist_.Size(), MPI_DOUBLE, MPI_COMM_WORLD,
                &tempSize);  // add size for wall distance
  sendBufSize += tempSize;
  MPI_Pack_size(avgWaveSpeed_.Size(), MPI_DOUBLE, MPI_COMM_WORLD,
                &tempSize);  // add size for average wave speed
  sendBufSize += tempSize;

  char *sendBuffer = new char[sendBufSize];  // allocate buffer to pack data
                                             // into

  // pack data to send into buffer
  int position = 0;
  MPI_Pack(&state_(0, 0, 0), state_.Size(), MPI_cellData, sendBuffer,
           sendBufSize, &position, MPI_COMM_WORLD);
  MPI_Pack(&residual_(0, 0, 0), residual_.Size(), MPI_cellData,
           sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
  MPI_Pack(&dt_(0, 0, 0), dt_.Size(), MPI_DOUBLE, sendBuffer,
           sendBufSize, &position, MPI_COMM_WORLD);
  MPI_Pack(&wallDist_(0, 0, 0), wallDist_.Size(), MPI_DOUBLE,
           sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
  MPI_Pack(&avgWaveSpeed_(0, 0, 0), avgWaveSpeed_.Size(), MPI_DOUBLE,
           sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);

  // send buffer to appropriate processor
  MPI_Send(sendBuffer, sendBufSize, MPI_PACKED, ROOTP, globalPos_,
           MPI_COMM_WORLD);

  delete[] sendBuffer;  // deallocate buffer
}

/* Member function to split a procBlock along a plane defined by a direction and
an index. The calling instance will retain the lower portion of the split,
and the returned instance will retain the upper portion of the split.
*/
procBlock procBlock::Split(const string &dir, const int &ind, const int &num,
                           vector<boundarySurface> &alteredSurf) {
  // dir -- plane to split along, either i, j, or k
  // ind -- index (face) to split at (w/o counting ghost cells)
  // num -- new block number
  // alteredSurf -- vector of surfaces whose partners will need to be altered
  // after this split

  boundaryConditions bound1 = bc_;
  boundaryConditions bound2 = bound1.Split(dir, ind, parBlock_, num,
                                           alteredSurf);

  if (dir == "i") {  // split along i-plane
    int numI2 = this->NumI() - ind;
    int numI1 = this->NumI() - numI2;

    procBlock blk1(numI1, this->NumJ(), this->NumK(), numGhosts_);
    procBlock blk2(numI2, this->NumJ(), this->NumK(), numGhosts_);

    blk1.parBlock_ = parBlock_;
    blk2.parBlock_ = parBlock_;

    // indices for lower block
    int iMaxG1 = numI1 + 2 * numGhosts_;
    int iMax1 = numI1;

    // indices for upper block
    int iMaxG2 = numI2 + 2 * numGhosts_;
    int iMax2 = numI2;

    int iMaxPG2 = this->NumI() + 2 * numGhosts_;
    int iMinPG2 = ind;
    int iMaxP2 = this->NumI();
    int iMinP2 = ind + numGhosts_;

    // indices common to both blocks
    int jMaxG = this->NumJ() + 2 * numGhosts_;
    int jMax = this->NumJ();
    int kMaxG = this->NumK() + 2 * numGhosts_;
    int kMax = this->NumK();

    // ------------------------------------------------------------------
    // assign variables for lower split
    // assign cell variables with ghost cells
    blk1.state_.Insert(0, iMaxG1, 0, jMaxG, 0, kMaxG,
                       state_.Slice(0, iMaxG1, 0, jMaxG, 0, kMaxG));
    blk1.vol_.Insert(0, iMaxG1, 0, jMaxG, 0, kMaxG,
                     vol_.Slice(0, iMaxG1, 0, jMaxG, 0, kMaxG));
    blk1.center_.Insert(0, iMaxG1, 0, jMaxG, 0, kMaxG,
                        center_.Slice(0, iMaxG1, 0, jMaxG, 0, kMaxG));
    // assign cell variables without ghost cells
    blk1.avgWaveSpeed_.Insert(0, iMax1, 0, jMax, 0, kMax,
                              avgWaveSpeed_.Slice(0, iMax1, 0, jMax, 0, kMax));
    blk1.dt_.Insert(0, iMax1, 0, jMax, 0, kMax,
                    dt_.Slice(0, iMax1, 0, jMax, 0, kMax));
    blk1.wallDist_.Insert(0, iMax1, 0, jMax, 0, kMax,
                          wallDist_.Slice(0, iMax1, 0, jMax, 0, kMax));
    blk1.residual_.Insert(0, iMax1, 0, jMax, 0, kMax,
                          residual_.Slice(0, iMax1, 0, jMax, 0, kMax));

    // assign face variables
    blk1.fAreaI_.Insert(0, iMaxG1 + 1, 0, jMaxG, 0, kMaxG,
                        fAreaI_.Slice(0, iMaxG1 + 1, 0, jMaxG, 0, kMaxG));
    blk1.fAreaJ_.Insert(0, iMaxG1, 0, jMaxG + 1, 0, kMaxG,
                        fAreaJ_.Slice(0, iMaxG1, 0, jMaxG + 1, 0, kMaxG));
    blk1.fAreaK_.Insert(0, iMaxG1, 0, jMaxG, 0, kMaxG + 1,
                        fAreaK_.Slice(0, iMaxG1, 0, jMaxG, 0, kMaxG + 1));

    blk1.fCenterI_.Insert(0, iMaxG1 + 1, 0, jMaxG, 0, kMaxG,
                        fCenterI_.Slice(0, iMaxG1 + 1, 0, jMaxG, 0, kMaxG));
    blk1.fCenterJ_.Insert(0, iMaxG1, 0, jMaxG + 1, 0, kMaxG,
                        fCenterJ_.Slice(0, iMaxG1, 0, jMaxG + 1, 0, kMaxG));
    blk1.fCenterK_.Insert(0, iMaxG1, 0, jMaxG, 0, kMaxG + 1,
                        fCenterK_.Slice(0, iMaxG1, 0, jMaxG, 0, kMaxG + 1));

    // ------------------------------------------------------------------
    // assign variables for upper split
    // assign cell variables with ghost cells
    blk2.state_.Insert(0, iMaxG2, 0, jMaxG, 0, kMaxG,
                       state_.Slice(iMinPG2, iMaxPG2, 0, jMaxG, 0, kMaxG));
    blk2.vol_.Insert(0, iMaxG2, 0, jMaxG, 0, kMaxG,
                       vol_.Slice(iMinPG2, iMaxPG2, 0, jMaxG, 0, kMaxG));
    blk2.center_.Insert(0, iMaxG2, 0, jMaxG, 0, kMaxG,
                       center_.Slice(iMinPG2, iMaxPG2, 0, jMaxG, 0, kMaxG));
    // assign cell variables without ghost cells
    blk2.avgWaveSpeed_.Insert(0, iMax2, 0, jMax, 0, kMax,
                              avgWaveSpeed_.Slice(iMinP2, iMaxP2, 0, jMax, 0,
                                                  kMax));
    blk2.dt_.Insert(0, iMax2, 0, jMax, 0, kMax,
                    dt_.Slice(iMinP2, iMaxP2, 0, jMax, 0, kMax));
    blk2.wallDist_.Insert(0, iMax2, 0, jMax, 0, kMax,
                          wallDist_.Slice(iMinP2, iMaxP2, 0, jMax, 0, kMax));
    blk2.residual_.Insert(0, iMax2, 0, jMax, 0, kMax,
                          residual_.Slice(iMinP2, iMaxP2, 0, jMax, 0, kMax));
    // assign face variables
    blk2.fAreaI_.Insert(0, iMaxG2 + 1, 0, jMaxG, 0, kMaxG,
                        fAreaI_.Slice(iMinPG2, iMaxPG2 + 1, 0, jMaxG, 0,
                                      kMaxG));
    blk2.fAreaJ_.Insert(0, iMaxG2, 0, jMaxG + 1, 0, kMaxG,
                        fAreaJ_.Slice(iMinPG2, iMaxPG2, 0, jMaxG + 1, 0,
                                      kMaxG));
    blk2.fAreaK_.Insert(0, iMaxG2, 0, jMaxG, 0, kMaxG + 1,
                        fAreaK_.Slice(iMinPG2, iMaxPG2, 0, jMaxG, 0,
                                      kMaxG + 1));

    blk2.fCenterI_.Insert(0, iMaxG2 + 1, 0, jMaxG, 0, kMaxG,
                          fCenterI_.Slice(iMinPG2, iMaxPG2 + 1, 0, jMaxG, 0,
                                          kMaxG));
    blk2.fCenterJ_.Insert(0, iMaxG2, 0, jMaxG + 1, 0, kMaxG,
                          fCenterJ_.Slice(iMinPG2, iMaxPG2, 0, jMaxG + 1, 0,
                                          kMaxG));
    blk2.fCenterK_.Insert(0, iMaxG2, 0, jMaxG, 0, kMaxG + 1,
                          fCenterK_.Slice(iMinPG2, iMaxPG2, 0, jMaxG, 0,
                                          kMaxG + 1));

    // assign boundary conditions
    blk1.bc_ = bound1;
    (*this) = blk1;
    blk2.bc_ = bound2;
    return blk2;

  } else if (dir == "j") {  // split along j-plane
    int numJ2 = this->NumJ() - ind;
    int numJ1 = this->NumJ() - numJ2;

    procBlock blk1(this->NumI(), numJ1, this->NumK(), numGhosts_);
    procBlock blk2(this->NumI(), numJ2, this->NumK(), numGhosts_);

    blk1.parBlock_ = parBlock_;
    blk2.parBlock_ = parBlock_;

    // indices for lower block
    int jMaxG1 = numJ1 + 2 * numGhosts_;
    int jMax1 = numJ1;

    // indices for upper block
    int jMaxG2 = numJ2 + 2 * numGhosts_;
    int jMax2 = numJ2;

    int jMaxPG2 = this->NumJ() + 2 * numGhosts_;
    int jMinPG2 = ind;
    int jMaxP2 = this->NumJ();
    int jMinP2 = ind + numGhosts_;

    // indices common to both blocks
    int iMaxG = this->NumI() + 2 * numGhosts_;
    int iMax = this->NumI();
    int kMaxG = this->NumK() + 2 * numGhosts_;
    int kMax = this->NumK();

    // ------------------------------------------------------------------
    // assign variables for lower split
    // assign cell variables with ghost cells
    blk1.state_.Insert(0, iMaxG, 0, jMaxG1, 0, kMaxG,
                       state_.Slice(0, iMaxG, 0, jMaxG1, 0, kMaxG));
    blk1.vol_.Insert(0, iMaxG, 0, jMaxG1, 0, kMaxG,
                     vol_.Slice(0, iMaxG, 0, jMaxG1, 0, kMaxG));
    blk1.center_.Insert(0, iMaxG, 0, jMaxG1, 0, kMaxG,
                        center_.Slice(0, iMaxG, 0, jMaxG1, 0, kMaxG));
    // assign cell variables without ghost cells
    blk1.avgWaveSpeed_.Insert(0, iMax, 0, jMax1, 0, kMax,
                              avgWaveSpeed_.Slice(0, iMax, 0, jMax1, 0, kMax));
    blk1.dt_.Insert(0, iMax, 0, jMax1, 0, kMax,
                    dt_.Slice(0, iMax, 0, jMax1, 0, kMax));
    blk1.wallDist_.Insert(0, iMax, 0, jMax1, 0, kMax,
                          wallDist_.Slice(0, iMax, 0, jMax1, 0, kMax));
    blk1.residual_.Insert(0, iMax, 0, jMax1, 0, kMax,
                          residual_.Slice(0, iMax, 0, jMax1, 0, kMax));

    // assign face variables
    blk1.fAreaI_.Insert(0, iMaxG + 1, 0, jMaxG1, 0, kMaxG,
                        fAreaI_.Slice(0, iMaxG + 1, 0, jMaxG1, 0, kMaxG));
    blk1.fAreaJ_.Insert(0, iMaxG, 0, jMaxG1 + 1, 0, kMaxG,
                        fAreaJ_.Slice(0, iMaxG, 0, jMaxG1 + 1, 0, kMaxG));
    blk1.fAreaK_.Insert(0, iMaxG, 0, jMaxG1, 0, kMaxG + 1,
                        fAreaK_.Slice(0, iMaxG, 0, jMaxG1, 0, kMaxG + 1));

    blk1.fCenterI_.Insert(0, iMaxG + 1, 0, jMaxG1, 0, kMaxG,
                        fCenterI_.Slice(0, iMaxG + 1, 0, jMaxG1, 0, kMaxG));
    blk1.fCenterJ_.Insert(0, iMaxG, 0, jMaxG1 + 1, 0, kMaxG,
                        fCenterJ_.Slice(0, iMaxG, 0, jMaxG1 + 1, 0, kMaxG));
    blk1.fCenterK_.Insert(0, iMaxG, 0, jMaxG1, 0, kMaxG + 1,
                        fCenterK_.Slice(0, iMaxG, 0, jMaxG1, 0, kMaxG + 1));

    // ------------------------------------------------------------------
    // assign variables for upper split
    // assign cell variables with ghost cells
    blk2.state_.Insert(0, iMaxG, 0, jMaxG2, 0, kMaxG,
                       state_.Slice(0, iMaxG, jMinPG2, jMaxPG2, 0, kMaxG));
    blk2.vol_.Insert(0, iMaxG, 0, jMaxG2, 0, kMaxG,
                       vol_.Slice(0, iMaxG, jMinPG2, jMaxPG2, 0, kMaxG));
    blk2.center_.Insert(0, iMaxG, 0, jMaxG2, 0, kMaxG,
                       center_.Slice(0, iMaxG, jMinPG2, jMaxPG2, 0, kMaxG));
    // assign cell variables without ghost cells
    blk2.avgWaveSpeed_.Insert(0, iMax, 0, jMax2, 0, kMax,
                              avgWaveSpeed_.Slice(0, iMax, jMinP2, jMaxP2, 0,
                                                  kMax));
    blk2.dt_.Insert(0, iMax, 0, jMax2, 0, kMax,
                    dt_.Slice(0, iMax, jMinP2, jMaxP2, 0, kMax));
    blk2.wallDist_.Insert(0, iMax, 0, jMax2, 0, kMax,
                          wallDist_.Slice(0, iMax, jMinP2, jMaxP2, 0, kMax));
    blk2.residual_.Insert(0, iMax, 0, jMax2, 0, kMax,
                          residual_.Slice(0, iMax, jMinP2, jMaxP2, 0, kMax));
    // assign face variables
    blk2.fAreaI_.Insert(0, iMaxG + 1, 0, jMaxG2, 0, kMaxG,
                        fAreaI_.Slice(0, iMaxG + 1, jMinPG2, jMaxPG2, 0,
                                      kMaxG));
    blk2.fAreaJ_.Insert(0, iMaxG, 0, jMaxG2 + 1, 0, kMaxG,
                        fAreaJ_.Slice(0, iMaxG, jMinPG2, jMaxPG2 + 1, 0,
                                      kMaxG));
    blk2.fAreaK_.Insert(0, iMaxG, 0, jMaxG2, 0, kMaxG + 1,
                        fAreaK_.Slice(0, iMaxG, jMinPG2, jMaxPG2, 0,
                                      kMaxG + 1));

    blk2.fCenterI_.Insert(0, iMaxG + 1, 0, jMaxG2, 0, kMaxG,
                          fCenterI_.Slice(0, iMaxG + 1, jMinPG2, jMaxPG2, 0,
                                          kMaxG));
    blk2.fCenterJ_.Insert(0, iMaxG, 0, jMaxG2 + 1, 0, kMaxG,
                          fCenterJ_.Slice(0, iMaxG, jMinPG2, jMaxPG2 + 1, 0,
                                          kMaxG));
    blk2.fCenterK_.Insert(0, iMaxG, 0, jMaxG2, 0, kMaxG + 1,
                          fCenterK_.Slice(0, iMaxG, jMinPG2, jMaxPG2, 0,
                                          kMaxG + 1));

    // assign boundary conditions
    blk1.bc_ = bound1;
    (*this) = blk1;
    blk2.bc_ = bound2;
    return blk2;

  } else if (dir == "k") {  // split along k-plane
    int numK2 = this->NumK() - ind;
    int numK1 = this->NumK() - numK2;

    procBlock blk1(this->NumI(), this->NumJ(), numK1, numGhosts_);
    procBlock blk2(this->NumI(), this->NumJ(), numK2, numGhosts_);

    blk1.parBlock_ = parBlock_;
    blk2.parBlock_ = parBlock_;

    // indices for lower block
    int kMaxG1 = numK1 + 2 * numGhosts_;
    int kMax1 = numK1;

    // indices for upper block
    int kMaxG2 = numK2 + 2 * numGhosts_;
    int kMax2 = numK2;

    int kMaxPG2 = this->NumK() + 2 * numGhosts_;
    int kMinPG2 = ind;
    int kMaxP2 = this->NumK();
    int kMinP2 = ind + numGhosts_;

    // indices common to both blocks
    int iMaxG = this->NumI() + 2 * numGhosts_;
    int iMax = this->NumI();
    int jMaxG = this->NumJ() + 2 * numGhosts_;
    int jMax = this->NumJ();

    // ------------------------------------------------------------------
    // assign variables for lower split
    // assign cell variables with ghost cells
    blk1.state_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxG1,
                       state_.Slice(0, iMaxG, 0, jMaxG, 0, kMaxG1));
    blk1.vol_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxG1,
                     vol_.Slice(0, iMaxG, 0, jMaxG, 0, kMaxG1));
    blk1.center_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxG1,
                        center_.Slice(0, iMaxG, 0, jMaxG, 0, kMaxG1));
    // assign cell variables without ghost cells
    blk1.avgWaveSpeed_.Insert(0, iMax, 0, jMax, 0, kMax1,
                              avgWaveSpeed_.Slice(0, iMax, 0, jMax, 0, kMax1));
    blk1.dt_.Insert(0, iMax, 0, jMax, 0, kMax1,
                    dt_.Slice(0, iMax, 0, jMax, 0, kMax1));
    blk1.wallDist_.Insert(0, iMax, 0, jMax, 0, kMax1,
                          wallDist_.Slice(0, iMax, 0, jMax, 0, kMax1));
    blk1.residual_.Insert(0, iMax, 0, jMax, 0, kMax1,
                          residual_.Slice(0, iMax, 0, jMax, 0, kMax1));

    // assign face variables
    blk1.fAreaI_.Insert(0, iMaxG + 1, 0, jMaxG, 0, kMaxG1,
                        fAreaI_.Slice(0, iMaxG + 1, 0, jMaxG, 0, kMaxG1));
    blk1.fAreaJ_.Insert(0, iMaxG, 0, jMaxG + 1, 0, kMaxG1,
                        fAreaJ_.Slice(0, iMaxG, 0, jMaxG + 1, 0, kMaxG1));
    blk1.fAreaK_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxG1 + 1,
                        fAreaK_.Slice(0, iMaxG, 0, jMaxG, 0, kMaxG1 + 1));

    blk1.fCenterI_.Insert(0, iMaxG + 1, 0, jMaxG, 0, kMaxG1,
                        fCenterI_.Slice(0, iMaxG + 1, 0, jMaxG, 0, kMaxG1));
    blk1.fCenterJ_.Insert(0, iMaxG, 0, jMaxG + 1, 0, kMaxG1,
                        fCenterJ_.Slice(0, iMaxG, 0, jMaxG + 1, 0, kMaxG1));
    blk1.fCenterK_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxG1 + 1,
                        fCenterK_.Slice(0, iMaxG, 0, jMaxG, 0, kMaxG1 + 1));

    // ------------------------------------------------------------------
    // assign variables for upper split
    // assign cell variables with ghost cells
    blk2.state_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxG2,
                       state_.Slice(0, iMaxG, 0, jMaxG, kMinPG2, kMaxPG2));
    blk2.vol_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxG2,
                       vol_.Slice(0, iMaxG, 0, jMaxG, kMinPG2, kMaxPG2));
    blk2.center_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxG2,
                       center_.Slice(0, iMaxG, 0, jMaxG, kMinPG2, kMaxPG2));
    // assign cell variables without ghost cells
    blk2.avgWaveSpeed_.Insert(0, iMax, 0, jMax, 0, kMax2,
                              avgWaveSpeed_.Slice(0, iMax, 0, jMax, kMinP2,
                                                  kMaxP2));
    blk2.dt_.Insert(0, iMax, 0, jMax, 0, kMax2,
                    dt_.Slice(0, iMax, 0, jMax, kMinP2, kMaxP2));
    blk2.wallDist_.Insert(0, iMax, 0, jMax, 0, kMax2,
                          wallDist_.Slice(0, iMax, 0, jMax, kMinP2, kMaxP2));
    blk2.residual_.Insert(0, iMax, 0, jMax, 0, kMax2,
                          residual_.Slice(0, iMax, 0, jMax, kMinP2, kMaxP2));
    // assign face variables
    blk2.fAreaI_.Insert(0, iMaxG + 1, 0, jMaxG, 0, kMaxG2,
                        fAreaI_.Slice(0, iMaxG + 1, 0, jMaxG, kMinPG2,
                                      kMaxPG2));
    blk2.fAreaJ_.Insert(0, iMaxG, 0, jMaxG + 1, 0, kMaxG2,
                        fAreaJ_.Slice(0, iMaxG, 0, jMaxG + 1, kMinPG2,
                                      kMaxPG2));
    blk2.fAreaK_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxG2 + 1,
                        fAreaK_.Slice(0, iMaxG, 0, jMaxG, kMinPG2,
                                      kMaxPG2 + 1));

    blk2.fCenterI_.Insert(0, iMaxG + 1, 0, jMaxG, 0, kMaxG2,
                          fCenterI_.Slice(0, iMaxG + 1, 0, jMaxG, kMinPG2,
                                          kMaxPG2));
    blk2.fCenterJ_.Insert(0, iMaxG, 0, jMaxG + 1, 0, kMaxG2,
                          fCenterJ_.Slice(0, iMaxG, 0, jMaxG + 1, kMinPG2,
                                          kMaxPG2));
    blk2.fCenterK_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxG2 + 1,
                          fCenterK_.Slice(0, iMaxG, 0, jMaxG, kMinPG2,
                                          kMaxPG2 + 1));

    // assign boundary conditions
    blk1.bc_ = bound1;
    (*this) = blk1;
    blk2.bc_ = bound2;
    return blk2;

  } else {
    cerr << "ERROR: Error in procBlock::Split(). Direction " << dir
         << " is not recognized! Choose either i, j, or k." << endl;
    exit(0);
  }
}

/* Member function to join a procBlock along a plane defined by a direction.
 * Assumes that calling instance is lower, and input instance is upper.*/
void procBlock::Join(const procBlock &blk, const string &dir,
                     vector<boundarySurface> &alteredSurf) {
  // blk -- block to join with
  // dir -- plane to split along, either i, j, or k
  // alteredSurf -- vector of surfaces whose partners will need to be altered
  // after this split

  if (dir == "i") {  // --------------------------------------------------
    int iMax = this->NumI() + blk.NumI();
    int jMax = this->NumJ();
    int kMax = this->NumK();

    int iMaxG = iMax + 2 * numGhosts_;
    int jMaxG = jMax + 2 * numGhosts_;
    int kMaxG = kMax + 2 * numGhosts_;

    procBlock newBlk(iMax, jMax, kMax, numGhosts_);

    newBlk.bc_ = bc_;
    newBlk.bc_.Join(blk.bc_, dir, alteredSurf);

    int iMaxUG = blk.NumI() + 2 * blk.numGhosts_;
    int iMaxU = blk.NumI();
    int iMaxLG = this->NumI() + blk.numGhosts_;  // don't copy upper ghosts
    int iMaxL = this->NumI();

    int iMinUG = numGhosts_;

    // assign variables from lower block -----------------------------
    // assign cell variables with ghost cells
    newBlk.state_.Insert(0, iMaxLG, 0, jMaxG, 0, kMaxG,
                         state_.Slice(0, iMaxLG, 0, jMaxG, 0, kMaxG));
    newBlk.vol_.Insert(0, iMaxLG, 0, jMaxG, 0, kMaxG,
                       vol_.Slice(0, iMaxLG, 0, jMaxG, 0, kMaxG));
    newBlk.center_.Insert(0, iMaxLG, 0, jMaxG, 0, kMaxG,
                          center_.Slice(0, iMaxLG, 0, jMaxG, 0, kMaxG));
    // assign cell variables without ghost cells
    newBlk.avgWaveSpeed_.Insert(0, iMaxL, 0, jMax, 0, kMax,
                                avgWaveSpeed_.Slice(0, iMaxL, 0, jMax, 0,
                                                    kMax));
    newBlk.dt_.Insert(0, iMaxL, 0, jMax, 0, kMax,
                      dt_.Slice(0, iMaxL, 0, jMax, 0, kMax));
    newBlk.wallDist_.Insert(0, iMaxL, 0, jMax, 0, kMax,
                            wallDist_.Slice(0, iMaxL, 0, jMax, 0, kMax));
    newBlk.residual_.Insert(0, iMaxL, 0, jMax, 0, kMax,
                            residual_.Slice(0, iMaxL, 0, jMax, 0, kMax));

    // assign face variables
    newBlk.fAreaI_.Insert(0, iMaxLG + 1, 0, jMaxG, 0, kMaxG,
                        fAreaI_.Slice(0, iMaxLG + 1, 0, jMaxG, 0, kMaxG));
    newBlk.fAreaJ_.Insert(0, iMaxLG, 0, jMaxG + 1, 0, kMaxG,
                        fAreaJ_.Slice(0, iMaxLG, 0, jMaxG + 1, 0, kMaxG));
    newBlk.fAreaK_.Insert(0, iMaxLG, 0, jMaxG, 0, kMaxG + 1,
                        fAreaK_.Slice(0, iMaxLG, 0, jMaxG, 0, kMaxG + 1));

    newBlk.fCenterI_.Insert(0, iMaxLG + 1, 0, jMaxG, 0, kMaxG,
                        fCenterI_.Slice(0, iMaxLG + 1, 0, jMaxG, 0, kMaxG));
    newBlk.fCenterJ_.Insert(0, iMaxLG, 0, jMaxG + 1, 0, kMaxG,
                        fCenterJ_.Slice(0, iMaxLG, 0, jMaxG + 1, 0, kMaxG));
    newBlk.fCenterK_.Insert(0, iMaxLG, 0, jMaxG, 0, kMaxG + 1,
                        fCenterK_.Slice(0, iMaxLG, 0, jMaxG, 0, kMaxG + 1));

    // assign variables from upper block -----------------------------
    // assign cell variables with ghost cells
    newBlk.state_.Insert(iMaxLG + 1, iMaxG, 0, jMaxG, 0, kMaxG,
                         blk.state_.Slice(iMinUG, iMaxUG, 0, jMaxG, 0, kMaxG));
    newBlk.vol_.Insert(iMaxLG + 1, iMaxG, 0, jMaxG, 0, kMaxG,
                       blk.vol_.Slice(iMinUG, iMaxUG, 0, jMaxG, 0, kMaxG));
    newBlk.center_.Insert(iMaxLG + 1, iMaxG, 0, jMaxG, 0, kMaxG,
                          blk.center_.Slice(iMinUG, iMaxUG, 0, jMaxG, 0,
                                            kMaxG));
    // assign cell variables without ghost cells
    newBlk.avgWaveSpeed_.Insert(iMaxL + 1, iMax, 0, jMax, 0, kMax,
                                blk.avgWaveSpeed_.Slice(0, iMaxU, 0, jMax, 0,
                                                    kMax));
    newBlk.dt_.Insert(iMaxL + 1, iMax, 0, jMax, 0, kMax,
                      blk.dt_.Slice(0, iMaxU, 0, jMax, 0, kMax));
    newBlk.wallDist_.Insert(iMaxL + 1, iMax, 0, jMax, 0, kMax,
                            blk.wallDist_.Slice(0, iMaxU, 0, jMax, 0, kMax));
    newBlk.residual_.Insert(iMaxL + 1, iMax, 0, jMax, 0, kMax,
                            blk.residual_.Slice(0, iMaxU, 0, jMax, 0, kMax));

    // assign face variables
    newBlk.fAreaI_.Insert(iMaxLG + 1, iMaxG + 1, 0, jMaxG, 0, kMaxG,
                        blk.fAreaI_.Slice(iMinUG, iMaxUG + 1, 0, jMaxG, 0,
                                          kMaxG));
    newBlk.fAreaJ_.Insert(iMaxLG + 1, iMaxG, 0, jMaxG + 1, 0, kMaxG,
                        blk.fAreaJ_.Slice(iMinUG, iMaxUG, 0, jMaxG + 1, 0,
                                          kMaxG));
    newBlk.fAreaK_.Insert(iMaxLG + 1, iMaxG, 0, jMaxG, 0, kMaxG + 1,
                        blk.fAreaK_.Slice(iMinUG, iMaxUG, 0, jMaxG, 0,
                                          kMaxG + 1));

    newBlk.fCenterI_.Insert(iMaxLG + 1, iMaxG + 1, 0, jMaxG, 0, kMaxG,
                        blk.fCenterI_.Slice(iMinUG, iMaxUG + 1, 0, jMaxG, 0,
                                            kMaxG));
    newBlk.fCenterJ_.Insert(iMaxLG + 1, iMaxG, 0, jMaxG + 1, 0, kMaxG,
                        blk.fCenterJ_.Slice(iMinUG, iMaxUG, 0, jMaxG + 1, 0,
                                            kMaxG));
    newBlk.fCenterK_.Insert(iMaxLG + 1, iMaxG, 0, jMaxG, 0, kMaxG + 1,
                        blk.fCenterK_.Slice(iMinUG, iMaxUG, 0, jMaxG, 0,
                                            kMaxG + 1));

    *this = newBlk;
  } else if (dir == "j") {  // -----------------------------------------
    int iMax = this->NumI();
    int jMax = this->NumJ() + blk.NumJ();
    int kMax = this->NumK();

    int iMaxG = iMax + 2 * numGhosts_;
    int jMaxG = jMax + 2 * numGhosts_;
    int kMaxG = kMax + 2 * numGhosts_;

    procBlock newBlk(iMax, jMax, kMax, numGhosts_);

    newBlk.bc_ = bc_;
    newBlk.bc_.Join(blk.bc_, dir, alteredSurf);

    int jMaxUG = blk.NumJ() + 2 * blk.numGhosts_;
    int jMaxU = blk.NumJ();
    int jMaxLG = this->NumJ() + blk.numGhosts_;  // don't copy upper ghosts
    int jMaxL = this->NumJ();

    int jMinUG = numGhosts_;

    // assign variables from lower block -----------------------------
    // assign cell variables with ghost cells
    newBlk.state_.Insert(0, iMaxG, 0, jMaxLG, 0, kMaxG,
                         state_.Slice(0, iMaxG, 0, jMaxLG, 0, kMaxG));
    newBlk.vol_.Insert(0, iMaxG, 0, jMaxLG, 0, kMaxG,
                       vol_.Slice(0, iMaxG, 0, jMaxLG, 0, kMaxG));
    newBlk.center_.Insert(0, iMaxG, 0, jMaxLG, 0, kMaxG,
                          center_.Slice(0, iMaxG, 0, jMaxLG, 0, kMaxG));
    // assign cell variables without ghost cells
    newBlk.avgWaveSpeed_.Insert(0, iMax, 0, jMaxL, 0, kMax,
                                avgWaveSpeed_.Slice(0, iMax, 0, jMaxL, 0,
                                                    kMax));
    newBlk.dt_.Insert(0, iMax, 0, jMaxL, 0, kMax,
                      dt_.Slice(0, iMax, 0, jMaxL, 0, kMax));
    newBlk.wallDist_.Insert(0, iMax, 0, jMaxL, 0, kMax,
                            wallDist_.Slice(0, iMax, 0, jMaxL, 0, kMax));
    newBlk.residual_.Insert(0, iMax, 0, jMaxL, 0, kMax,
                            residual_.Slice(0, iMax, 0, jMaxL, 0, kMax));

    // assign face variables
    newBlk.fAreaI_.Insert(0, iMaxG + 1, 0, jMaxLG, 0, kMaxG,
                        fAreaI_.Slice(0, iMaxG + 1, 0, jMaxLG, 0, kMaxG));
    newBlk.fAreaJ_.Insert(0, iMaxG, 0, jMaxLG + 1, 0, kMaxG,
                        fAreaJ_.Slice(0, iMaxG, 0, jMaxLG + 1, 0, kMaxG));
    newBlk.fAreaK_.Insert(0, iMaxG, 0, jMaxLG, 0, kMaxG + 1,
                        fAreaK_.Slice(0, iMaxG, 0, jMaxLG, 0, kMaxG + 1));

    newBlk.fCenterI_.Insert(0, iMaxG + 1, 0, jMaxLG, 0, kMaxG,
                        fCenterI_.Slice(0, iMaxG + 1, 0, jMaxLG, 0, kMaxG));
    newBlk.fCenterJ_.Insert(0, iMaxG, 0, jMaxLG + 1, 0, kMaxG,
                        fCenterJ_.Slice(0, iMaxG, 0, jMaxLG + 1, 0, kMaxG));
    newBlk.fCenterK_.Insert(0, iMaxG, 0, jMaxLG, 0, kMaxG + 1,
                        fCenterK_.Slice(0, iMaxG, 0, jMaxLG, 0, kMaxG + 1));

    // assign variables from upper block -----------------------------
    // assign cell variables with ghost cells
    newBlk.state_.Insert(0, iMaxG, jMaxLG + 1, jMaxG, 0, kMaxG,
                         blk.state_.Slice(0, iMaxG, jMinUG, jMaxUG, 0, kMaxG));
    newBlk.vol_.Insert(0, iMaxG, jMaxLG + 1, jMaxG, 0, kMaxG,
                       blk.vol_.Slice(0, iMaxG, jMinUG, jMaxUG, 0, kMaxG));
    newBlk.center_.Insert(0, iMaxG, jMaxLG + 1, jMaxG, 0, kMaxG,
                          blk.center_.Slice(0, iMaxG, jMinUG, jMaxUG, 0,
                                            kMaxG));
    // assign cell variables without ghost cells
    newBlk.avgWaveSpeed_.Insert(0, iMax, jMaxL + 1, jMax, 0, kMax,
                                blk.avgWaveSpeed_.Slice(0, iMax, 0, jMaxU, 0,
                                                    kMax));
    newBlk.dt_.Insert(0, iMax, jMaxL + 1, jMax, 0, kMax,
                      blk.dt_.Slice(0, iMax, 0, jMaxU, 0, kMax));
    newBlk.wallDist_.Insert(0, iMax, jMaxL + 1, jMax, 0, kMax,
                            blk.wallDist_.Slice(0, iMax, 0, jMaxU, 0, kMax));
    newBlk.residual_.Insert(0, iMax, jMaxL + 1, jMax, 0, kMax,
                            blk.residual_.Slice(0, iMax, 0, jMaxU, 0, kMax));

    // assign face variables
    newBlk.fAreaI_.Insert(0, iMaxG + 1, jMaxLG + 1, jMaxG, 0, kMaxG,
                        blk.fAreaI_.Slice(0, iMaxG + 1, jMinUG, jMaxUG, 0,
                                          kMaxG));
    newBlk.fAreaJ_.Insert(0, iMaxG, jMaxLG + 1, jMaxG + 1, 0, kMaxG,
                        blk.fAreaJ_.Slice(0, iMaxG, jMinUG, jMaxUG + 1, 0,
                                          kMaxG));
    newBlk.fAreaK_.Insert(0, iMaxG, jMaxLG + 1, jMaxG, 0, kMaxG + 1,
                        blk.fAreaK_.Slice(0, iMaxG, jMinUG, jMaxUG, 0,
                                          kMaxG + 1));

    newBlk.fCenterI_.Insert(0, iMaxG + 1, jMaxLG + 1, jMaxG, 0, kMaxG,
                        blk.fCenterI_.Slice(0, iMaxG + 1, jMinUG, jMaxUG, 0,
                                            kMaxG));
    newBlk.fCenterJ_.Insert(0, iMaxG, jMaxLG + 1, jMaxG + 1, 0, kMaxG,
                        blk.fCenterJ_.Slice(0, iMaxG, jMinUG, jMaxUG + 1, 0,
                                            kMaxG));
    newBlk.fCenterK_.Insert(0, iMaxG, jMaxLG + 1, jMaxG, 0, kMaxG + 1,
                        blk.fCenterK_.Slice(0, iMaxG, jMinUG, jMaxUG, 0,
                                            kMaxG + 1));

    *this = newBlk;
  } else if (dir == "k") {  // ----------------------------------------------
    int iMax = this->NumI();
    int jMax = this->NumJ();
    int kMax = this->NumK() + blk.NumK();

    int iMaxG = iMax + 2 * numGhosts_;
    int jMaxG = jMax + 2 * numGhosts_;
    int kMaxG = kMax + 2 * numGhosts_;

    procBlock newBlk(iMax, jMax, kMax, numGhosts_);

    newBlk.bc_ = bc_;
    newBlk.bc_.Join(blk.bc_, dir, alteredSurf);

    int kMaxUG = blk.NumK() + 2 * blk.numGhosts_;
    int kMaxU = blk.NumK();
    int kMaxLG = this->NumK() + blk.numGhosts_;  // don't copy upper ghosts
    int kMaxL = this->NumK();

    int kMinUG = numGhosts_;

    // assign variables from lower block -----------------------------
    // assign cell variables with ghost cells
    newBlk.state_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxLG,
                         state_.Slice(0, iMaxG, 0, jMaxG, 0, kMaxLG));
    newBlk.vol_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxLG,
                       vol_.Slice(0, iMaxG, 0, jMaxG, 0, kMaxLG));
    newBlk.center_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxLG,
                          center_.Slice(0, iMaxG, 0, jMaxG, 0, kMaxLG));
    // assign cell variables without ghost cells
    newBlk.avgWaveSpeed_.Insert(0, iMax, 0, jMax, 0, kMaxL,
                                avgWaveSpeed_.Slice(0, iMax, 0, jMax, 0,
                                                    kMaxL));
    newBlk.dt_.Insert(0, iMax, 0, jMax, 0, kMaxL,
                      dt_.Slice(0, iMax, 0, jMax, 0, kMaxL));
    newBlk.wallDist_.Insert(0, iMax, 0, jMax, 0, kMaxL,
                            wallDist_.Slice(0, iMax, 0, jMax, 0, kMaxL));
    newBlk.residual_.Insert(0, iMax, 0, jMax, 0, kMaxL,
                            residual_.Slice(0, iMax, 0, jMax, 0, kMaxL));

    // assign face variables
    newBlk.fAreaI_.Insert(0, iMaxG + 1, 0, jMaxG, 0, kMaxLG,
                        fAreaI_.Slice(0, iMaxG + 1, 0, jMaxG, 0, kMaxLG));
    newBlk.fAreaJ_.Insert(0, iMaxG, 0, jMaxG + 1, 0, kMaxLG,
                        fAreaJ_.Slice(0, iMaxG, 0, jMaxG + 1, 0, kMaxLG));
    newBlk.fAreaK_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxLG + 1,
                        fAreaK_.Slice(0, iMaxG, 0, jMaxG, 0, kMaxLG + 1));

    newBlk.fCenterI_.Insert(0, iMaxG + 1, 0, jMaxG, 0, kMaxLG,
                        fCenterI_.Slice(0, iMaxG + 1, 0, jMaxG, 0, kMaxLG));
    newBlk.fCenterJ_.Insert(0, iMaxG, 0, jMaxG + 1, 0, kMaxLG,
                        fCenterJ_.Slice(0, iMaxG, 0, jMaxG + 1, 0, kMaxLG));
    newBlk.fCenterK_.Insert(0, iMaxG, 0, jMaxG, 0, kMaxLG + 1,
                        fCenterK_.Slice(0, iMaxG, 0, jMaxG, 0, kMaxLG + 1));

    // assign variables from upper block -----------------------------
    // assign cell variables with ghost cells
    newBlk.state_.Insert(0, iMaxG, 0, jMaxG, kMaxLG + 1, kMaxG,
                         blk.state_.Slice(0, iMaxG, 0, jMaxG, kMinUG, kMaxUG));
    newBlk.vol_.Insert(0, iMaxG, 0, jMaxG, kMaxLG + 1, kMaxG,
                       blk.vol_.Slice(0, iMaxG, 0, jMaxG, kMinUG, kMaxUG));
    newBlk.center_.Insert(0, iMaxG, 0, jMaxG, kMaxLG + 1, kMaxG,
                          blk.center_.Slice(0, iMaxG, 0, jMaxG, kMinUG,
                                            kMaxUG));
    // assign cell variables without ghost cells
    newBlk.avgWaveSpeed_.Insert(0, iMax, 0, jMax, kMaxL + 1, kMax,
                                blk.avgWaveSpeed_.Slice(0, iMax, 0, jMax, 0,
                                                    kMaxU));
    newBlk.dt_.Insert(0, iMax, 0, jMax, kMaxL + 1, kMax,
                      blk.dt_.Slice(0, iMax, 0, jMax, 0, kMaxU));
    newBlk.wallDist_.Insert(0, iMax, 0, jMax, kMaxL + 1, kMax,
                            blk.wallDist_.Slice(0, iMax, 0, jMax, 0, kMaxU));
    newBlk.residual_.Insert(0, iMax, 0, jMax, kMaxL + 1, kMax,
                            blk.residual_.Slice(0, iMax, 0, jMax, 0, kMaxU));

    // assign face variables
    newBlk.fAreaI_.Insert(0, iMaxG + 1, 0, jMaxG, kMaxLG + 1, kMaxG,
                        blk.fAreaI_.Slice(0, iMaxG + 1, 0, jMaxG, kMinUG,
                                          kMaxUG));
    newBlk.fAreaJ_.Insert(0, iMaxG, 0, jMaxG + 1, kMaxLG + 1, kMaxG,
                        blk.fAreaJ_.Slice(0, iMaxG, 0, jMaxG + 1, kMinUG,
                                          kMaxUG));
    newBlk.fAreaK_.Insert(0, iMaxG, 0, jMaxG, kMaxLG + 1, kMaxG + 1,
                        blk.fAreaK_.Slice(0, iMaxG, 0, jMaxG, kMinUG,
                                          kMaxUG + 1));

    newBlk.fCenterI_.Insert(0, iMaxG + 1, 0, jMaxG, kMaxLG + 1, kMaxG,
                        blk.fCenterI_.Slice(0, iMaxG + 1, 0, jMaxG, kMinUG,
                                            kMaxUG));
    newBlk.fCenterJ_.Insert(0, iMaxG, 0, jMaxG + 1, kMaxLG + 1, kMaxG,
                        blk.fCenterJ_.Slice(0, iMaxG, 0, jMaxG + 1, kMinUG,
                                            kMaxUG));
    newBlk.fCenterK_.Insert(0, iMaxG, 0, jMaxG, kMaxLG + 1, kMaxG + 1,
                        blk.fCenterK_.Slice(0, iMaxG, 0, jMaxG, kMinUG,
                                            kMaxUG + 1));

    *this = newBlk;
  } else {
    cerr << "ERROR: Error in procBlock::Join(). Direction " << dir
         << " is not recognized! Choose either i, j, or k." << endl;
    exit(0);
  }
}

void procBlock::CalcGradsI(const int &ii, const int &jj, const int &kk,
                           const idealGas &eqnState, const bool &turbFlag,
                           tensor<double> &velGrad, vector3d<double> &tGrad,
                           vector3d<double> &tkeGrad,
                           vector3d<double> &omegaGrad) const {
  // ii -- i-index for face (including ghosts)
  // jj -- j-index for face (including ghosts)
  // kk -- k-index for face (including ghosts)
  // eqnState -- equation of state
  // turbFlag -- flag to determine if simulation is turbulent
  // velGrad -- tensor to store velocity gradient
  // tGrad -- vector3d to store temperature gradient
  // tkeGrad -- vector3d to store tke gradient
  // omegaGrad -- vector3d to store omega gradient

  // calculate areas of faces in alternate control volume
  vector3d<double> aiu = 0.5 *
      (fAreaI_(ii, jj, kk).Vector() + fAreaI_(ii + 1, jj, kk).Vector());
  vector3d<double> ail = 0.5 *
      (fAreaI_(ii, jj, kk).Vector() + fAreaI_(ii - 1, jj, kk).Vector());

  vector3d<double> aju = 0.5 *
      (fAreaJ_(ii, jj + 1, kk).Vector() + fAreaJ_(ii - 1, jj + 1, kk).Vector());
  vector3d<double> ajl = 0.5 *
      (fAreaJ_(ii, jj, kk).Vector() + fAreaJ_(ii - 1, jj, kk).Vector());

  vector3d<double> aku = 0.5 *
      (fAreaK_(ii, jj, kk + 1).Vector() + fAreaK_(ii - 1, jj, kk + 1).Vector());
  vector3d<double> akl = 0.5 *
      (fAreaK_(ii, jj, kk).Vector() +  fAreaK_(ii - 1, jj, kk).Vector());

  // calculate volume of alternate control volume
  double vol = 0.5 * (vol_(ii - 1, jj, kk) + vol_(ii, jj, kk));

  // calculate average velocity on j and k faces of alternate control volume
  vector3d<double> vju = 0.25 *
      (state_(ii - 1, jj, kk).Velocity() + state_(ii, jj, kk).Velocity() +
       state_(ii, jj + 1, kk).Velocity() +
       state_(ii - 1, jj + 1, kk).Velocity());
  vector3d<double> vjl = 0.25 *
      (state_(ii - 1, jj, kk).Velocity() + state_(ii, jj, kk).Velocity() +
       state_(ii, jj - 1, kk).Velocity() +
       state_(ii - 1, jj - 1, kk).Velocity());

  vector3d<double> vku = 0.25 *
      (state_(ii - 1, jj, kk).Velocity() + state_(ii, jj, kk).Velocity() +
       state_(ii, jj, kk + 1).Velocity() +
       state_(ii - 1, jj, kk + 1).Velocity());
  vector3d<double> vkl = 0.25 *
      (state_(ii - 1, jj, kk).Velocity() + state_(ii, jj, kk).Velocity() +
       state_(ii, jj, kk - 1).Velocity() +
       state_(ii - 1, jj, kk - 1).Velocity());

  // Get velocity gradient at face
  velGrad = CalcVelGradGG(state_(ii - 1, jj, kk).Velocity(),
                          state_(ii, jj, kk).Velocity(), vjl, vju, vkl, vku,
                          ail, aiu, ajl, aju, akl, aku, vol);

  // calculate average temperature on j and k faces of alternate control volume
  double tju = 0.25 * (state_(ii - 1, jj, kk).Temperature(eqnState) +
                       state_(ii, jj, kk).Temperature(eqnState) +
                       state_(ii, jj + 1, kk).Temperature(eqnState) +
                       state_(ii - 1, jj + 1, kk).Temperature(eqnState));
  double tjl = 0.25 * (state_(ii - 1, jj, kk).Temperature(eqnState) +
                       state_(ii, jj, kk).Temperature(eqnState) +
                       state_(ii, jj - 1, kk).Temperature(eqnState) +
                       state_(ii - 1, jj - 1, kk).Temperature(eqnState));

  double tku = 0.25 * (state_(ii - 1, jj, kk).Temperature(eqnState) +
                       state_(ii, jj, kk).Temperature(eqnState) +
                       state_(ii, jj, kk + 1).Temperature(eqnState) +
                       state_(ii - 1, jj, kk + 1).Temperature(eqnState));
  double tkl = 0.25 * (state_(ii - 1, jj, kk).Temperature(eqnState) +
                       state_(ii, jj, kk).Temperature(eqnState) +
                       state_(ii, jj, kk - 1).Temperature(eqnState) +
                       state_(ii - 1, jj, kk - 1).Temperature(eqnState));

  // Get temperature gradient at face
  tGrad = CalcScalarGradGG(state_(ii - 1, jj, kk).Temperature(eqnState),
                           state_(ii, jj, kk).Temperature(eqnState), tjl, tju,
                           tkl, tku, ail, aiu, ajl, aju, akl, aku, vol);

  if (turbFlag) {
    // calculate average tke on j and k faces of alternate control volume
    double tkeju = 0.25 *
        (state_(ii - 1, jj, kk).Tke() + state_(ii, jj, kk).Tke() +
         state_(ii, jj + 1, kk).Tke() + state_(ii - 1, jj + 1, kk).Tke());
    double tkejl = 0.25 *
        (state_(ii - 1, jj, kk).Tke() + state_(ii, jj, kk).Tke() +
         state_(ii, jj - 1, kk).Tke() + state_(ii - 1, jj - 1, kk).Tke());

    double tkeku = 0.25 *
        (state_(ii - 1, jj, kk).Tke() + state_(ii, jj, kk).Tke() +
         state_(ii, jj, kk + 1).Tke() + state_(ii - 1, jj, kk + 1).Tke());
    double tkekl = 0.25 *
        (state_(ii - 1, jj, kk).Tke() + state_(ii, jj, kk).Tke() +
         state_(ii, jj, kk - 1).Tke() + state_(ii - 1, jj, kk - 1).Tke());

    // Get tke gradient at face
    tkeGrad = CalcScalarGradGG(state_(ii - 1, jj, kk).Tke(),
                               state_(ii, jj, kk).Tke(), tkejl, tkeju, tkekl,
                               tkeku, ail, aiu, ajl, aju, akl, aku, vol);

    // calculate average Omega on j and k faces of alternate control volume
    double omgju = 0.25 *
        (state_(ii - 1, jj, kk).Omega() + state_(ii, jj, kk).Omega() +
         state_(ii, jj + 1, kk).Omega() + state_(ii - 1, jj + 1, kk).Omega());
    double omgjl = 0.25 *
        (state_(ii - 1, jj, kk).Omega() + state_(ii, jj, kk).Omega() +
         state_(ii, jj - 1, kk).Omega() + state_(ii - 1, jj - 1, kk).Omega());

    double omgku = 0.25 *
        (state_(ii - 1, jj, kk).Omega() + state_(ii, jj, kk).Omega() +
         state_(ii, jj, kk + 1).Omega() + state_(ii - 1, jj, kk + 1).Omega());
    double omgkl = 0.25 *
        (state_(ii - 1, jj, kk).Omega() + state_(ii, jj, kk).Omega() +
         state_(ii, jj, kk - 1).Omega() + state_(ii - 1, jj, kk - 1).Omega());

    // Get omega gradient at face
    omegaGrad = CalcScalarGradGG(
        state_(ii - 1, jj, kk).Omega(), state_(ii, jj, kk).Omega(), omgjl,
        omgju, omgkl, omgku, ail, aiu, ajl, aju, akl, aku, vol);
  }
}

void procBlock::CalcGradsJ(const int &ii, const int &jj, const int &kk,
                           const idealGas &eqnState, const bool &turbFlag,
                           tensor<double> &velGrad, vector3d<double> &tGrad,
                           vector3d<double> &tkeGrad,
                           vector3d<double> &omegaGrad) const {
  // ii -- i-index for face (including ghosts)
  // jj -- j-index for face (including ghosts)
  // kk -- k-index for face (including ghosts)
  // eqnState -- equation of state
  // turbFlag -- flag to determine if simulation is turbulent
  // velGrad -- tensor to store velocity gradient
  // tGrad -- vector3d to store temperature gradient
  // tkeGrad -- vector3d to store tke gradient
  // omegaGrad -- vector3d to store omega gradient

  // calculate areas of faces in alternate control volume
  vector3d<double> aju = 0.5 *
      (fAreaJ_(ii, jj, kk).Vector() + fAreaJ_(ii, jj + 1, kk).Vector());
  vector3d<double> ajl = 0.5 *
      (fAreaJ_(ii, jj, kk).Vector() + fAreaJ_(ii, jj - 1, kk).Vector());

  vector3d<double> aiu = 0.5 *
      (fAreaI_(ii + 1, jj, kk).Vector() + fAreaI_(ii + 1, jj - 1, kk).Vector());
  vector3d<double> ail = 0.5 *
      (fAreaI_(ii, jj, kk).Vector() + fAreaI_(ii, jj - 1, kk).Vector());

  vector3d<double> aku = 0.5 *
      (fAreaK_(ii, jj, kk + 1).Vector() + fAreaK_(ii, jj - 1, kk + 1).Vector());
  vector3d<double> akl = 0.5 *
      (fAreaK_(ii, jj, kk).Vector() + fAreaK_(ii, jj - 1, kk).Vector());

  // calculate volume of alternate control volume
  double vol = 0.5 * (vol_(ii, jj - 1, kk) + vol_(ii, jj, kk));

  // calculate average velocity on i and k faces of alternate control volume
  vector3d<double> viu = 0.25 *
      (state_(ii, jj - 1, kk).Velocity() + state_(ii, jj, kk).Velocity() +
       state_(ii + 1, jj, kk).Velocity() +
       state_(ii + 1, jj - 1, kk).Velocity());
  vector3d<double> vil = 0.25 *
      (state_(ii, jj - 1, kk).Velocity() + state_(ii, jj, kk).Velocity() +
       state_(ii - 1, jj, kk).Velocity() +
       state_(ii - 1, jj - 1, kk).Velocity());

  vector3d<double> vku = 0.25 *
      (state_(ii, jj - 1, kk).Velocity() + state_(ii, jj, kk).Velocity() +
       state_(ii, jj, kk + 1).Velocity() +
       state_(ii, jj - 1, kk + 1).Velocity());
  vector3d<double> vkl = 0.25 *
      (state_(ii, jj - 1, kk).Velocity() + state_(ii, jj, kk).Velocity() +
       state_(ii, jj, kk - 1).Velocity() +
       state_(ii, jj - 1, kk - 1).Velocity());

  // Get velocity gradient at face
  velGrad = CalcVelGradGG(vil, viu, state_(ii, jj - 1, kk).Velocity(),
                          state_(ii, jj, kk).Velocity(), vkl, vku, ail, aiu,
                          ajl, aju, akl, aku, vol);

  // calculate average temperature on i and k faces of alternate control volume
  double tiu = 0.25 * (state_(ii, jj - 1, kk).Temperature(eqnState) +
                       state_(ii, jj, kk).Temperature(eqnState) +
                       state_(ii + 1, jj, kk).Temperature(eqnState) +
                       state_(ii + 1, jj - 1, kk).Temperature(eqnState));
  double til = 0.25 * (state_(ii, jj - 1, kk).Temperature(eqnState) +
                       state_(ii, jj, kk).Temperature(eqnState) +
                       state_(ii - 1, jj, kk).Temperature(eqnState) +
                       state_(ii - 1, jj - 1, kk).Temperature(eqnState));

  double tku = 0.25 * (state_(ii, jj - 1, kk).Temperature(eqnState) +
                       state_(ii, jj, kk).Temperature(eqnState) +
                       state_(ii, jj, kk + 1).Temperature(eqnState) +
                       state_(ii, jj - 1, kk + 1).Temperature(eqnState));
  double tkl = 0.25 * (state_(ii, jj - 1, kk).Temperature(eqnState) +
                       state_(ii, jj, kk).Temperature(eqnState) +
                       state_(ii, jj, kk - 1).Temperature(eqnState) +
                       state_(ii, jj - 1, kk - 1).Temperature(eqnState));

  // Get temperature gradient at face
  tGrad = CalcScalarGradGG(til, tiu,
                           state_(ii, jj - 1, kk).Temperature(eqnState),
                           state_(ii, jj, kk).Temperature(eqnState), tkl, tku,
                           ail, aiu, ajl, aju, akl, aku, vol);

  if (turbFlag) {
    // calculate average tke on i and k faces of alternate control volume
    double tkeiu = 0.25 *
        (state_(ii, jj - 1, kk).Tke() + state_(ii, jj, kk).Tke() +
         state_(ii + 1, jj, kk).Tke() + state_(ii + 1, jj - 1, kk).Tke());
    double tkeil = 0.25 *
        (state_(ii, jj - 1, kk).Tke() + state_(ii, jj, kk).Tke() +
         state_(ii - 1, jj, kk).Tke() + state_(ii - 1, jj - 1, kk).Tke());

    double tkeku = 0.25 *
        (state_(ii, jj - 1, kk).Tke() + state_(ii, jj, kk).Tke() +
         state_(ii, jj, kk + 1).Tke() + state_(ii, jj - 1, kk + 1).Tke());
    double tkekl = 0.25 *
        (state_(ii, jj - 1, kk).Tke() + state_(ii, jj, kk).Tke() +
         state_(ii, jj, kk - 1).Tke() + state_(ii, jj - 1, kk - 1).Tke());

    // Get temperature gradient at face
    tkeGrad = CalcScalarGradGG(tkeil, tkeiu, state_(ii, jj - 1, kk).Tke(),
                               state_(ii, jj, kk).Tke(), tkekl, tkeku, ail, aiu,
                               ajl, aju, akl, aku, vol);

    // calculate average omega on i and k faces of alternate control volume
    double omgiu = 0.25 *
        (state_(ii, jj - 1, kk).Omega() + state_(ii, jj, kk).Omega() +
         state_(ii + 1, jj, kk).Omega() + state_(ii + 1, jj - 1, kk).Omega());
    double omgil = 0.25 *
        (state_(ii, jj - 1, kk).Omega() + state_(ii, jj, kk).Omega() +
         state_(ii - 1, jj, kk).Omega() + state_(ii - 1, jj - 1, kk).Omega());

    double omgku = 0.25 *
        (state_(ii, jj - 1, kk).Omega() + state_(ii, jj, kk).Omega() +
         state_(ii, jj, kk + 1).Omega() + state_(ii, jj - 1, kk + 1).Omega());
    double omgkl = 0.25 *
        (state_(ii, jj - 1, kk).Omega() + state_(ii, jj, kk).Omega() +
         state_(ii, jj, kk - 1).Omega() + state_(ii, jj - 1, kk - 1).Omega());

    // Get temperature gradient at face
    omegaGrad = CalcScalarGradGG(omgil, omgiu, state_(ii, jj - 1, kk).Omega(),
                                 state_(ii, jj, kk).Omega(), omgkl, omgku, ail,
                                 aiu, ajl, aju, akl, aku, vol);
  }
}

void procBlock::CalcGradsK(const int &ii, const int &jj, const int &kk,
                           const idealGas &eqnState, const bool &turbFlag,
                           tensor<double> &velGrad, vector3d<double> &tGrad,
                           vector3d<double> &tkeGrad,
                           vector3d<double> &omegaGrad) const {
  // ii -- i-index for face (including ghosts)
  // jj -- j-index for face (including ghosts)
  // kk -- k-index for face (including ghosts)
  // eqnState -- equation of state
  // turbFlag -- flag to determine if simulation is turbulent
  // velGrad -- tensor to store velocity gradient
  // tGrad -- vector3d to store temperature gradient
  // tkeGrad -- vector3d to store tke gradient
  // omegaGrad -- vector3d to store omega gradient

  // calculate areas of faces in alternate control volume
  vector3d<double> aku = 0.5 *
      (fAreaK_(ii, jj, kk).Vector() + fAreaK_(ii, jj, kk + 1).Vector());
  vector3d<double> akl = 0.5 *
      (fAreaK_(ii, jj, kk).Vector() + fAreaK_(ii, jj, kk - 1).Vector());

  vector3d<double> aiu = 0.5 *
      (fAreaI_(ii + 1, jj, kk).Vector() + fAreaI_(ii + 1, jj, kk - 1).Vector());
  vector3d<double> ail = 0.5 *
      (fAreaI_(ii, jj, kk).Vector() + fAreaI_(ii, jj, kk - 1).Vector());

  vector3d<double> aju = 0.5 *
      (fAreaJ_(ii, jj + 1, kk).Vector() + fAreaJ_(ii, jj + 1, kk - 1).Vector());
  vector3d<double> ajl = 0.5 *
      (fAreaJ_(ii, jj, kk).Vector() + fAreaJ_(ii, jj, kk - 1).Vector());

  // calculate volume of alternate control volume
  double vol = 0.5 * (vol_(ii, jj, kk - 1) + vol_(ii, jj, kk));

  // calculate average velocity on i and j faces of alternate control volume
  vector3d<double> viu = 0.25 *
      (state_(ii, jj, kk - 1).Velocity() + state_(ii, jj, kk).Velocity() +
       state_(ii + 1, jj, kk).Velocity() +
       state_(ii + 1, jj, kk - 1).Velocity());
  vector3d<double> vil = 0.25 *
      (state_(ii, jj, kk - 1).Velocity() + state_(ii, jj, kk).Velocity() +
       state_(ii - 1, jj, kk).Velocity() +
       state_(ii - 1, jj, kk - 1).Velocity());

  vector3d<double> vju = 0.25 *
      (state_(ii, jj, kk - 1).Velocity() + state_(ii, jj, kk).Velocity() +
       state_(ii, jj, kk + 1).Velocity() +
       state_(ii, jj + 1, kk - 1).Velocity());
  vector3d<double> vjl = 0.25 *
      (state_(ii, jj, kk - 1).Velocity() + state_(ii, jj, kk).Velocity() +
       state_(ii, jj - 1, kk).Velocity() +
       state_(ii, jj - 1, kk - 1).Velocity());

  // Get velocity gradient at face
  velGrad = CalcVelGradGG(vil, viu, vjl, vju, state_(ii, jj, kk - 1).Velocity(),
                          state_(ii, jj, kk).Velocity(), ail, aiu, ajl, aju,
                          akl, aku, vol);

  // calculate average temperature on i and j faces of alternate control volume
  double tiu = 0.25 * (state_(ii, jj, kk - 1).Temperature(eqnState) +
                       state_(ii, jj, kk).Temperature(eqnState) +
                       state_(ii + 1, jj, kk).Temperature(eqnState) +
                       state_(ii + 1, jj, kk - 1).Temperature(eqnState));
  double til = 0.25 * (state_(ii, jj, kk - 1).Temperature(eqnState) +
                       state_(ii, jj, kk).Temperature(eqnState) +
                       state_(ii - 1, jj, kk).Temperature(eqnState) +
                       state_(ii - 1, jj, kk - 1).Temperature(eqnState));

  double tju = 0.25 * (state_(ii, jj, kk - 1).Temperature(eqnState) +
                       state_(ii, jj, kk).Temperature(eqnState) +
                       state_(ii, jj, kk + 1).Temperature(eqnState) +
                       state_(ii, jj + 1, kk - 1).Temperature(eqnState));
  double tjl = 0.25 * (state_(ii, jj, kk - 1).Temperature(eqnState) +
                       state_(ii, jj, kk).Temperature(eqnState) +
                       state_(ii, jj - 1, kk).Temperature(eqnState) +
                       state_(ii, jj - 1, kk - 1).Temperature(eqnState));

  // Get temperature gradient at face
  tGrad = CalcScalarGradGG(til, tiu, tjl, tju,
                           state_(ii, jj, kk - 1).Temperature(eqnState),
                           state_(ii, jj, kk).Temperature(eqnState), ail, aiu,
                           ajl, aju, akl, aku, vol);

  if (turbFlag) {
    // calculate average tke on i and j faces of alternate control volume
    double tkeiu = 0.25 *
        (state_(ii, jj, kk - 1).Tke() + state_(ii, jj, kk).Tke() +
         state_(ii + 1, jj, kk).Tke() + state_(ii + 1, jj, kk - 1).Tke());
    double tkeil = 0.25 *
        (state_(ii, jj, kk - 1).Tke() + state_(ii, jj, kk).Tke() +
         state_(ii - 1, jj, kk).Tke() + state_(ii - 1, jj, kk - 1).Tke());

    double tkeju = 0.25 *
        (state_(ii, jj, kk - 1).Tke() + state_(ii, jj, kk).Tke() +
         state_(ii, jj, kk + 1).Tke() + state_(ii, jj + 1, kk - 1).Tke());
    double tkejl = 0.25 *
        (state_(ii, jj, kk - 1).Tke() + state_(ii, jj, kk).Tke() +
         state_(ii, jj - 1, kk).Tke() + state_(ii, jj - 1, kk - 1).Tke());

    // Get temperature gradient at face
    tkeGrad = CalcScalarGradGG(
        tkeil, tkeiu, tkejl, tkeju, state_(ii, jj, kk - 1).Tke(),
        state_(ii, jj, kk).Tke(), ail, aiu, ajl, aju, akl, aku, vol);

    // calculate average omega on i and j faces of alternate control volume
    double omgiu = 0.25 *
        (state_(ii, jj, kk - 1).Omega() + state_(ii, jj, kk).Omega() +
         state_(ii + 1, jj, kk).Omega() + state_(ii + 1, jj, kk - 1).Omega());
    double omgil = 0.25 *
        (state_(ii, jj, kk - 1).Omega() + state_(ii, jj, kk).Omega() +
         state_(ii - 1, jj, kk).Omega() + state_(ii - 1, jj, kk - 1).Omega());

    double omgju = 0.25 *
        (state_(ii, jj, kk - 1).Omega() + state_(ii, jj, kk).Omega() +
         state_(ii, jj, kk + 1).Omega() + state_(ii, jj + 1, kk - 1).Omega());
    double omgjl = 0.25 *
        (state_(ii, jj, kk - 1).Omega() + state_(ii, jj, kk).Omega() +
         state_(ii, jj - 1, kk).Omega() + state_(ii, jj - 1, kk - 1).Omega());

    // Get temperature gradient at face
    omegaGrad = CalcScalarGradGG(
        omgil, omgiu, omgjl, omgju, state_(ii, jj, kk - 1).Omega(),
        state_(ii, jj, kk).Omega(), ail, aiu, ajl, aju, akl, aku, vol);
  }
}

// Member function to calculate the source terms and add them to the residual
void procBlock::CalcSrcTerms(const gradients &grads, const sutherland &suth,
                             const turbModel *turb) {
  // grads -- gradients (vel, temp, tke, omega)
  // suth -- sutherland's law for viscosity
  // turb -- turbulence model

  // loop over all physical cells - no ghost cells needed for source terms
  for (struct {int p; int g;} kk = {0, numGhosts_}; kk.p <
           this->NumK(); kk.g++, kk.p++) {
    for (struct {int p; int g;} jj = {0, numGhosts_}; jj.p <
             this->NumJ(); jj.g++, jj.p++) {
      for (struct {int p; int g;} ii = {0, numGhosts_}; ii.p <
               this->NumI(); ii.g++, ii.p++) {
        // calculate turbulent source terms
        source src;
        src.CalcTurbSrc(turb, state_(ii.g, jj.g, kk.g), grads, suth,
                        ii.p, jj.p, kk.p);

        // add source terms to residual
        // multiply by -1 because residual is initially on opposite
        // side of equation
        this->AddToResidual(src * (-1.0 * vol_(ii.g, jj.g, kk.g)),
                            ii.p, jj.p, kk.p);
      }
    }
  }
}
