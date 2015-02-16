#include "parallel.h"

/* Function to return processor list for manual decomposition. Manual decomposition assumes that each block will reside on it's own processor.
The processor list tells how many procBlocks a processor will have.
*/
vector<int> ManualDecomposition(vector<procBlock> &grid, const int &numProc, vector<interblock> &connections, const int &totalCells ){
  // grid -- vector of procBlocks (no need to split procBlocks or combine them with manual decomposition)
  // numProc -- number of processors in run
  // connections -- vector of interblocks, these are updated to have the correct rank after decomposition
  // totalCells -- total number of cells in the grid; used for load balancing metrics

  if ( (int)grid.size() != numProc ){
    cerr << "ERROR: Error in parallel.cpp:ManualDecomposition(). Manual decomposition assumes that the number of processors used is equal to the " <<
      "number of blocks in the grid. This grid has " << grid.size() << " blocks and the simulation is using " << numProc << " processors." << endl;
    exit(0);
  }

  cout << "--------------------------------------------------------------------------------" << endl;
  cout << "Using manual grid decomposition." << endl;

  double idealLoad = (double)totalCells / (double)numProc; //average number of cells per processor
  int maxLoad = 0;

  //vector containing number of procBlocks for each processor
  //in manual decomp, each proc gets 1 block
  vector<int> loadBal(numProc, 1);

  //assign processor rank for each procBlock
  for ( int ii = 0; ii < numProc; ii++ ){
    grid[ii].SetRank(ii);
  }

  //assign global position for each procBlock
  for ( unsigned int ii = 0; ii < grid.size(); ii++ ){
    grid[ii].SetGlobalPos(ii);
    maxLoad = std::max(maxLoad, grid[ii].NumCells());
  }

  cout << "Ratio of most loaded processor to average processor is : " << (double)maxLoad / idealLoad << endl;
  cout << "--------------------------------------------------------------------------------" << endl << endl;;

  //adjust interblocks to have appropriate rank
  for ( unsigned int ii = 0; ii < connections.size(); ii++ ){
    connections[ii].SetRankFirst(grid[connections[ii].BlockFirst()].Rank());
    connections[ii].SetRankSecond(grid[connections[ii].BlockSecond()].Rank());
  }

  return loadBal;
}

//function to send each processor the number of procBlocks that it should contain
void SendNumProcBlocks(const vector<int> &loadBal, const int &rank, int &numProcBlock){
  MPI_Scatter(&loadBal[0], 1, MPI_INT, &numProcBlock, 1, MPI_INT, ROOT, MPI_COMM_WORLD);
}

//function to send each processor the vector of interblocks it needs to compute its boundary conditions
void SendConnections(vector<interblock> &connections, const MPI_Datatype &MPI_interblock) {

  //first determine the number of interblocks and send that to all processors
  int numCon = connections.size();
  MPI_Bcast(&numCon, 1, MPI_INT, ROOT, MPI_COMM_WORLD);

  connections.resize(numCon); //allocate space to receive the interblocks

  //broadcast all interblocks to all processors
  MPI_Bcast(&connections[0], connections.size(), MPI_interblock, ROOT, MPI_COMM_WORLD);
}

/* Function to set custom MPI datatypes to allow for easier data transmission */
void SetDataTypesMPI(const int &numEqn, MPI_Datatype &MPI_vec3d, MPI_Datatype &MPI_cellData, MPI_Datatype &MPI_procBlockInts, MPI_Datatype &MPI_interblock,
		     MPI_Datatype & MPI_DOUBLE_5INT ){
  // numEqn -- number of equations being solved
  // MPI_vec3d -- output MPI_Datatype for a vector3d<double>
  // MPI_cellData -- output MPI_Datatype for primVars or genArray
  // MPI_procBlockInts -- output MPI_Datatype for 14 INTs (14 INTs in procBlock class)
  // MPI_interblock -- output MPI_Datatype to send interblock class
  // MPI_DOUBLE_5INT -- output MPI_Datatype for a double followed by 5 ints

  //create vector3d<double> MPI datatype
  MPI_Type_contiguous(3, MPI_DOUBLE, &MPI_vec3d);
  MPI_Type_commit(&MPI_vec3d);

  //create MPI datatype for states (primVars), residuals (genArray), etc
  MPI_Type_contiguous(numEqn, MPI_DOUBLE, &MPI_cellData);
  MPI_Type_commit(&MPI_cellData);

  //create MPI datatype for all the integers in the procBlock class
  MPI_Type_contiguous(15, MPI_INT, &MPI_procBlockInts);
  MPI_Type_commit(&MPI_procBlockInts);


  //create MPI datatype for a double followed by 5 ints
  int fieldCounts[2] = {1,5}; //number of entries per field
  MPI_Datatype fieldTypes[2] = {MPI_DOUBLE, MPI_INT}; //field types
  MPI_Aint displacement[2], lBound, ext;
  resid res; //dummy resid to get layout of class
  //get addresses of each field
  MPI_Get_address(&res.linf,  &displacement[0]);
  MPI_Get_address(&res.blk,   &displacement[1]);
  //make addresses relative to first field
  for ( int ii = 1; ii >= 0; ii-- ){
    displacement[ii] -= displacement[0];
  }
  MPI_Type_create_struct(2, fieldCounts, displacement, fieldTypes, &MPI_DOUBLE_5INT);

  //check that datatype has the correct extent, if it doesn't change the extent
  //this is necessary to portably send an array of this type
  MPI_Type_get_extent(MPI_DOUBLE_5INT, &lBound, &ext);
  if ( ext != sizeof(res) ){
    MPI_Datatype temp = MPI_DOUBLE_5INT;
    MPI_Type_create_resized(temp, 0, sizeof(res), &MPI_DOUBLE_5INT);
    MPI_Type_free(&temp);
  }

  MPI_Type_commit(&MPI_DOUBLE_5INT);


  //create MPI datatype for interblock class
  int counts[10] = {2,2,2,2,2,2,2,2,2,1}; //number of entries per field
  MPI_Datatype types[10] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT}; //field types
  MPI_Aint disp[10], lowerBound, extent;
  interblock inter; //dummy interblock to get layout of class
  //get addresses of each field
  MPI_Get_address(&inter.rank[0],       &disp[0]);
  MPI_Get_address(&inter.block[0],      &disp[1]);
  MPI_Get_address(&inter.localBlock[0], &disp[2]);
  MPI_Get_address(&inter.boundary[0],   &disp[3]);
  MPI_Get_address(&inter.d1Start[0],    &disp[4]);
  MPI_Get_address(&inter.d1End[0],      &disp[5]);
  MPI_Get_address(&inter.d2Start[0],    &disp[6]);
  MPI_Get_address(&inter.d2End[0],      &disp[7]);
  MPI_Get_address(&inter.constSurf[0],  &disp[8]);
  MPI_Get_address(&inter.orientation,   &disp[9]);
  //make addresses relative to first field
  for ( int ii = 9; ii >= 0; ii-- ){
    disp[ii] -= disp[0];
  }
  MPI_Type_create_struct(10, counts, disp, types, &MPI_interblock);

  //check that datatype has the correct extent, if it doesn't change the extent
  //this is necessary to portably send an array of this type
  MPI_Type_get_extent(MPI_interblock, &lowerBound, &extent);
  if ( extent != sizeof(inter) ){
    MPI_Datatype temp = MPI_interblock;
    MPI_Type_create_resized(temp, 0, sizeof(inter), &MPI_interblock);
    MPI_Type_free(&temp);
  }

  MPI_Type_commit(&MPI_interblock);

}

/* Function to send procBlocks to their appropriate processor. This function is called after the decomposition has been run. The procBlock data all 
resides on the ROOT processor. In this function, the ROOT processor packs the procBlocks and sends them to the appropriate processor. All the non-ROOT
processors receive and unpack the data from ROOT. This is used to send the geometric block data from ROOT to all the processors at the beginning of the
simulation.
*/
vector<procBlock> SendProcBlocks( const vector<procBlock> &blocks, const int &rank, const int &numProcBlock, 
				  const MPI_Datatype &MPI_cellData, const MPI_Datatype &MPI_vec3d ){

  // blocks -- full vector of all procBlocks. This is only used on ROOT processor, all other processors just need a dummy variable to call the function
  // rank -- processor rank. Used to determine if process should send or receive
  // numProcBlock -- number of procBlocks that the processor should have. (All processors may give different values).
  // MPI_cellData -- MPI_Datatype used for primVars and genArray transmission
  // MPI_vec3d -- MPI_Datatype used for vector3d<double>  transmission

  vector<procBlock> localBlocks; //vector of procBlocks for each processor
  localBlocks.reserve(numProcBlock); //each processor may allocate for a different size

  //------------------------------------------------------------------------------------------------------------------------------------------------
  //                                                                ROOT
  //------------------------------------------------------------------------------------------------------------------------------------------------
  if ( rank == ROOT ){ // may have to pack and send data
    for ( unsigned int ii = 0; ii < blocks.size(); ii++ ){ //loop over ALL blocks

      if (blocks[ii].Rank() == ROOT ){ //no need to send data because it is already on ROOT processor
	localBlocks.push_back(blocks[ii]);
      }
      else{ //send data to receiving processors

	//copy INTs from procBlock so that they can be packed/sent (private class data cannot be packed/sent)
	int tempS[15];
	tempS[0]  = blocks[ii].NumCells();
	tempS[1]  = blocks[ii].NumVars();
	tempS[2]  = blocks[ii].NumI();
	tempS[3]  = blocks[ii].NumJ();
	tempS[4]  = blocks[ii].NumK();
	tempS[5]  = blocks[ii].NumGhosts();
	tempS[6]  = blocks[ii].ParentBlock();
	tempS[7]  = blocks[ii].ParentBlockStartI();
	tempS[8]  = blocks[ii].ParentBlockEndI();
	tempS[9]  = blocks[ii].ParentBlockStartJ();
	tempS[10] = blocks[ii].ParentBlockEndJ();
	tempS[11] = blocks[ii].ParentBlockStartK();
	tempS[12]  = blocks[ii].ParentBlockEndK();
	tempS[13]  = blocks[ii].Rank();
	tempS[14]  = blocks[ii].GlobalPos();

	//get copy of vector data that needs to be sent (private class data cannot be packed/sent)
	vector<primVars> primVecS = blocks[ii].StateVec(); 
	vector<vector3d<double> > centerVecS = blocks[ii].CenterVec(); 
	vector<vector3d<double> > fAreaIVecS = blocks[ii].FAreaIVec(); 
	vector<vector3d<double> > fAreaJVecS = blocks[ii].FAreaJVec(); 
	vector<vector3d<double> > fAreaKVecS = blocks[ii].FAreaKVec(); 
	vector<vector3d<double> > fCenterIVecS = blocks[ii].FCenterIVec(); 
	vector<vector3d<double> > fCenterJVecS = blocks[ii].FCenterJVec(); 
	vector<vector3d<double> > fCenterKVecS = blocks[ii].FCenterKVec(); 
	vector<double> volVecS = blocks[ii].VolVec(); 

	//get copy of boundary condition data that needs to be sent (private class data cannot be packed/sent)
	boundaryConditions boundS = blocks[ii].BC();
	int surfsS [3] = {boundS.NumSurfI(), boundS.NumSurfJ(), boundS.NumSurfK()};
	vector<string> namesS = boundS.GetBCTypesVec();
	vector<int> iMinS = boundS.GetIMinVec();
	vector<int> iMaxS = boundS.GetIMaxVec();
	vector<int> jMinS = boundS.GetJMinVec();
	vector<int> jMaxS = boundS.GetJMaxVec();
	vector<int> kMinS = boundS.GetKMinVec();
	vector<int> kMaxS = boundS.GetKMaxVec();
	vector<int> tagsS = boundS.GetTagVec();

	//get string lengths for each boundary condition to be sent, so processors unpacking know how much data to unpack for each string
	vector<int> strLengthS(boundS.NumSurfI() + boundS.NumSurfJ() + boundS.NumSurfK() );
	for ( unsigned int jj = 0; jj < strLengthS.size(); jj++ ){
	  strLengthS[jj] = boundS.GetBCTypes(jj).size();
	}

	//determine size of buffer to send
	int sendBufSize = 0;
	int tempSize = 0;
	MPI_Pack_size(15, MPI_INT, MPI_COMM_WORLD, &tempSize); //add size for ints in class procBlock
	sendBufSize += tempSize;
	MPI_Pack_size(primVecS.size(), MPI_cellData, MPI_COMM_WORLD, &tempSize); //add size for states
	sendBufSize += tempSize;
	MPI_Pack_size(centerVecS.size(), MPI_vec3d, MPI_COMM_WORLD, &tempSize); //add size for cell centers
	sendBufSize += tempSize;
	MPI_Pack_size(fAreaIVecS.size(), MPI_vec3d, MPI_COMM_WORLD, &tempSize); //add size for face area I
	sendBufSize += tempSize;
	MPI_Pack_size(fAreaJVecS.size(), MPI_vec3d, MPI_COMM_WORLD, &tempSize); //add size for face area J
	sendBufSize += tempSize;
	MPI_Pack_size(fAreaKVecS.size(), MPI_vec3d, MPI_COMM_WORLD, &tempSize); //add size for face area K
	sendBufSize += tempSize;
	MPI_Pack_size(fCenterIVecS.size(), MPI_vec3d, MPI_COMM_WORLD, &tempSize); //add size for face center I
	sendBufSize += tempSize;
	MPI_Pack_size(fCenterJVecS.size(), MPI_vec3d, MPI_COMM_WORLD, &tempSize); //add size for face center J
	sendBufSize += tempSize;
	MPI_Pack_size(fCenterKVecS.size(), MPI_vec3d, MPI_COMM_WORLD, &tempSize); //add size for face center K
	sendBufSize += tempSize;
	MPI_Pack_size(volVecS.size(), MPI_DOUBLE, MPI_COMM_WORLD, &tempSize); //add size for volumes
	sendBufSize += tempSize;
	MPI_Pack_size(3, MPI_INT, MPI_COMM_WORLD, &tempSize); //add size for number of surfaces
	sendBufSize += tempSize;
	//8x because iMin, iMax, jMin, jMax, kMin, kMax, tags, string sizes
	MPI_Pack_size( (boundS.NumSurfI() + boundS.NumSurfJ() + boundS.NumSurfK()) * 8, MPI_INT, MPI_COMM_WORLD, &tempSize); //add size for BCs
	sendBufSize += tempSize;
	int stringSize = 0;
	for ( int jj = 0; jj < (boundS.NumSurfI() + boundS.NumSurfJ() + boundS.NumSurfK()); jj++ ){
	  MPI_Pack_size(boundS.GetBCTypes(jj).size(), MPI_CHAR, MPI_COMM_WORLD, &tempSize); //add size for bc types
	  stringSize += tempSize;
	}
	sendBufSize += stringSize;

	char *sendBuffer = new char[sendBufSize]; //allocate buffer to pack data into

	//pack data to send into buffer
	int position = 0;
	//int and vector data
	MPI_Pack(&tempS[0], 15, MPI_INT, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&primVecS[0], primVecS.size(), MPI_cellData, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&centerVecS[0], centerVecS.size(), MPI_vec3d, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&fAreaIVecS[0], fAreaIVecS.size(), MPI_vec3d, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&fAreaJVecS[0], fAreaJVecS.size(), MPI_vec3d, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&fAreaKVecS[0], fAreaKVecS.size(), MPI_vec3d, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&fCenterIVecS[0], fCenterIVecS.size(), MPI_vec3d, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&fCenterJVecS[0], fCenterJVecS.size(), MPI_vec3d, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&fCenterKVecS[0], fCenterKVecS.size(), MPI_vec3d, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&volVecS[0], volVecS.size(), MPI_DOUBLE, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);

	//boundary condition data
	MPI_Pack(&surfsS[0], 3, MPI_INT, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&iMinS[0], iMinS.size(), MPI_INT, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&iMaxS[0], iMaxS.size(), MPI_INT, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&jMinS[0], jMinS.size(), MPI_INT, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&jMaxS[0], jMaxS.size(), MPI_INT, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&kMinS[0], kMinS.size(), MPI_INT, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&kMaxS[0], kMaxS.size(), MPI_INT, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&tagsS[0], tagsS.size(), MPI_INT, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	MPI_Pack(&strLengthS[0], strLengthS.size(), MPI_INT, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	for ( int jj = 0; jj < (boundS.NumSurfI() + boundS.NumSurfJ() + boundS.NumSurfK()); jj++ ){
	  MPI_Pack(namesS[jj].c_str(), namesS[jj].size(), MPI_CHAR, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
	}

	//send buffer to appropriate processor
	MPI_Send(sendBuffer, sendBufSize, MPI_PACKED, blocks[ii].Rank(), 2, MPI_COMM_WORLD);

	delete [] sendBuffer; //deallocate buffer

      }
    }

  }
  
  //------------------------------------------------------------------------------------------------------------------------------------------------
  //                                                                NON - ROOT
  //------------------------------------------------------------------------------------------------------------------------------------------------
  else { // receive and unpack data (non-root)
    for ( int ii = 0; ii < numProcBlock; ii++ ){

      MPI_Status status; //allocate MPI_Status structure
      int tempR[15]; //allocate space to receive procBlock INTs

      //probe message to get correct data size
      int bufRecvData = 0;
      MPI_Probe(ROOT, 2, MPI_COMM_WORLD, &status);
      MPI_Get_count(&status, MPI_CHAR, &bufRecvData); //use MPI_CHAR because sending buffer was allocated with chars

      char *recvBuffer = new char[bufRecvData]; //allocate buffer of correct size

      //receive message from ROOT
      MPI_Recv(recvBuffer, bufRecvData, MPI_PACKED, ROOT, 2, MPI_COMM_WORLD, &status);

      //unpack procBlock INTs
      int position = 0;
      MPI_Unpack(recvBuffer, bufRecvData, &position, &tempR[0], 15, MPI_INT, MPI_COMM_WORLD); //unpack ints

      //put procBlock INTs into new procBlock
      procBlock tempBlock(tempR[2], tempR[3], tempR[4], tempR[5]); //allocate procBlock for appropriate size (ni, nj, nk, nghosts)
      tempBlock.SetParentBlock(tempR[6]);
      tempBlock.SetParentBlockStartI(tempR[7]);
      tempBlock.SetParentBlockEndI(tempR[8]);
      tempBlock.SetParentBlockStartJ(tempR[9]);
      tempBlock.SetParentBlockEndJ(tempR[10]);
      tempBlock.SetParentBlockStartK(tempR[11]);
      tempBlock.SetParentBlockEndK(tempR[12]);
      tempBlock.SetRank(rank);
      tempBlock.SetGlobalPos(tempR[14]);

      int numCells = (tempR[2] + 2 * tempR[5]) * (tempR[3] + 2 * tempR[5]) * (tempR[4] + 2 * tempR[5]);
      int numFaceI = (tempR[2] + 2 * tempR[5] + 1) * (tempR[3] + 2 * tempR[5]) * (tempR[4] + 2 * tempR[5]);
      int numFaceJ = (tempR[2] + 2 * tempR[5]) * (tempR[3] + 2 * tempR[5] + 1) * (tempR[4] + 2 * tempR[5]);
      int numFaceK = (tempR[2] + 2 * tempR[5]) * (tempR[3] + 2 * tempR[5]) * (tempR[4] + 2 * tempR[5] + 1);

      //allocate vector data to appropriate size
      vector<primVars> primVecR(numCells);
      vector<vector3d<double> > centerVecR(numCells);
      vector<vector3d<double> > fAreaIVecR(numFaceI);
      vector<vector3d<double> > fAreaJVecR(numFaceJ);
      vector<vector3d<double> > fAreaKVecR(numFaceK);
      vector<vector3d<double> > fCenterIVecR(numFaceI);
      vector<vector3d<double> > fCenterJVecR(numFaceJ);
      vector<vector3d<double> > fCenterKVecR(numFaceK);
      vector<double> volVecR(numCells);

      //unpack vector data into allocated vectors
      MPI_Unpack(recvBuffer, bufRecvData, &position, &primVecR[0], primVecR.size(), MPI_cellData, MPI_COMM_WORLD); //unpack states
      MPI_Unpack(recvBuffer, bufRecvData, &position, &centerVecR[0], centerVecR.size(), MPI_vec3d, MPI_COMM_WORLD); //unpack cell centers
      MPI_Unpack(recvBuffer, bufRecvData, &position, &fAreaIVecR[0], fAreaIVecR.size(), MPI_vec3d, MPI_COMM_WORLD); //unpack face area I
      MPI_Unpack(recvBuffer, bufRecvData, &position, &fAreaJVecR[0], fAreaJVecR.size(), MPI_vec3d, MPI_COMM_WORLD); //unpack face area J
      MPI_Unpack(recvBuffer, bufRecvData, &position, &fAreaKVecR[0], fAreaKVecR.size(), MPI_vec3d, MPI_COMM_WORLD); //unpack face area K
      MPI_Unpack(recvBuffer, bufRecvData, &position, &fCenterIVecR[0], fCenterIVecR.size(), MPI_vec3d, MPI_COMM_WORLD); //unpack face center I
      MPI_Unpack(recvBuffer, bufRecvData, &position, &fCenterJVecR[0], fCenterJVecR.size(), MPI_vec3d, MPI_COMM_WORLD); //unpack face center J
      MPI_Unpack(recvBuffer, bufRecvData, &position, &fCenterKVecR[0], fCenterKVecR.size(), MPI_vec3d, MPI_COMM_WORLD); //unpack face center K
      MPI_Unpack(recvBuffer, bufRecvData, &position, &volVecR[0], volVecR.size(), MPI_DOUBLE, MPI_COMM_WORLD); //unpack volumes

      //assign unpacked vector data to procBlock
      tempBlock.SetStateVec(primVecR); //assign states
      tempBlock.SetCenterVec(centerVecR); //assign cell centers
      tempBlock.SetFAreaIVec(fAreaIVecR); //assign face area I
      tempBlock.SetFAreaJVec(fAreaJVecR); //assign face area J
      tempBlock.SetFAreaKVec(fAreaKVecR); //assign face area K
      tempBlock.SetFCenterIVec(fCenterIVecR); //assign face center I
      tempBlock.SetFCenterJVec(fCenterJVecR); //assign face center J
      tempBlock.SetFCenterKVec(fCenterKVecR); //assign face center K
      tempBlock.SetVolVec(volVecR); //assign volumes

      int surfsR[3]; //allocate space for boundary condition surfaces
      MPI_Unpack(recvBuffer, bufRecvData, &position, &surfsR[0], 3, MPI_INT, MPI_COMM_WORLD); //unpack number of surfaces

      //allocate boundRaryConditions to appropriate size
      boundaryConditions boundR(surfsR[0], surfsR[1], surfsR[2]);
      vector<int> iMinR(surfsR[0] + surfsR[1] + surfsR[2]);
      vector<int> iMaxR(surfsR[0] + surfsR[1] + surfsR[2]);
      vector<int> jMinR(surfsR[0] + surfsR[1] + surfsR[2]);
      vector<int> jMaxR(surfsR[0] + surfsR[1] + surfsR[2]);
      vector<int> kMinR(surfsR[0] + surfsR[1] + surfsR[2]);
      vector<int> kMaxR(surfsR[0] + surfsR[1] + surfsR[2]);
      vector<int> tagsR(surfsR[0] + surfsR[1] + surfsR[2]);
      vector<int> strLengthR(surfsR[0] + surfsR[1] + surfsR[2]);
      vector<string> namesR(surfsR[0] + surfsR[1] + surfsR[2]);

      //unpack boundary condition data into appropriate vectors
      MPI_Unpack(recvBuffer, bufRecvData, &position, &iMinR[0], iMinR.size(), MPI_INT, MPI_COMM_WORLD); //unpack i min coordinates
      MPI_Unpack(recvBuffer, bufRecvData, &position, &iMaxR[0], iMaxR.size(), MPI_INT, MPI_COMM_WORLD); //unpack i max coordinates
      MPI_Unpack(recvBuffer, bufRecvData, &position, &jMinR[0], jMinR.size(), MPI_INT, MPI_COMM_WORLD); //unpack j min coordinates
      MPI_Unpack(recvBuffer, bufRecvData, &position, &jMaxR[0], jMaxR.size(), MPI_INT, MPI_COMM_WORLD); //unpack j max coordinates
      MPI_Unpack(recvBuffer, bufRecvData, &position, &kMinR[0], kMinR.size(), MPI_INT, MPI_COMM_WORLD); //unpack k min coordinates
      MPI_Unpack(recvBuffer, bufRecvData, &position, &kMaxR[0], kMaxR.size(), MPI_INT, MPI_COMM_WORLD); //unpack k max coordinates
      MPI_Unpack(recvBuffer, bufRecvData, &position, &tagsR[0], tagsR.size(), MPI_INT, MPI_COMM_WORLD); //unpack tags
      MPI_Unpack(recvBuffer, bufRecvData, &position, &strLengthR[0], strLengthR.size(), MPI_INT, MPI_COMM_WORLD); //unpack string sizes
      //unpack boundary condition names
      for ( unsigned int jj = 0; jj < strLengthR.size(); jj++ ){
	char *nameBuf = new char[strLengthR[jj]]; //allocate buffer to store BC name
      	MPI_Unpack(recvBuffer, bufRecvData, &position, &nameBuf[0], strLengthR[jj], MPI_CHAR, MPI_COMM_WORLD); //unpack bc types
      	string bcName(nameBuf, strLengthR[jj]); //create string of bc name
      	namesR[jj] = bcName;
	delete [] nameBuf; //deallocate bc name buffer
      }

      delete [] recvBuffer; //deallocate receiving buffer

      //set up boundary conditions for procBlock
      boundR.SetBCTypesVec(namesR);
      boundR.SetIMinVec(iMinR);
      boundR.SetIMaxVec(iMaxR);
      boundR.SetJMinVec(jMinR);
      boundR.SetJMaxVec(jMaxR);
      boundR.SetKMinVec(kMinR);
      boundR.SetKMaxVec(kMaxR);
      boundR.SetTagVec(tagsR);
      tempBlock.SetBCs(boundR); //assign BCs to procBlock

      localBlocks.push_back(tempBlock); //add procBlock to output vector

    }
  }

  return localBlocks;

}


/* Function to send procBlocks to the root processor. In this function, the non-ROOT processors pack the procBlocks and send them to the ROOT processor. 
The ROOT processor receives and unpacks the data from the non-ROOT processors. This is used to get all the data on the ROOT processor to write out results.
*/
void GetProcBlocks( vector<procBlock> &blocks, const vector<procBlock> &localBlocks, const int &rank, const MPI_Datatype &MPI_cellData ){

  // blocks -- full vector of all procBlocks. This is only used on ROOT processor, all other processors just need a dummy variable to call the function
  // localBlocks -- procBlocks local to each processor. These are sent to ROOT
  // rank -- processor rank. Used to determine if process should send or receive
  // MPI_cellData -- MPI_Datatype used for primVars and genArray transmission

  //------------------------------------------------------------------------------------------------------------------------------------------------
  //                                                                ROOT
  //------------------------------------------------------------------------------------------------------------------------------------------------
  if ( rank == ROOT ){ // may have to recv and unpack data
    int locNum = 0;

    for ( unsigned int ii = 0; ii < blocks.size(); ii++ ){ //loop over ALL blocks

      if (blocks[ii].Rank() == ROOT ){ //no need to recv data because it is already on ROOT processor
	//assign local state block to global state block in order of local state vector
	blocks[ii] = localBlocks[locNum];
	locNum++;
      }
      else{ //recv data from sending processors

	MPI_Status status; //allocate MPI_Status structure

	//probe message to get correct data size
	int bufRecvData = 0;
	MPI_Probe(blocks[ii].Rank(), blocks[ii].GlobalPos(), MPI_COMM_WORLD, &status); //global position used as tag because each block has a unique one
	MPI_Get_count(&status, MPI_CHAR, &bufRecvData); //use MPI_CHAR because sending buffer was allocated with chars

	char *recvBuffer = new char[bufRecvData]; //allocate buffer of correct size

	//receive message from non-ROOT
	MPI_Recv(recvBuffer, bufRecvData, MPI_PACKED, blocks[ii].Rank(), blocks[ii].GlobalPos(), MPI_COMM_WORLD, &status);

	//calculate number of cells with and without ghost cells
	int numCells = (blocks[ii].NumI() + 2 * blocks[ii].NumGhosts()) * (blocks[ii].NumJ() + 2 * blocks[ii].NumGhosts()) * (blocks[ii].NumK() + 2 * blocks[ii].NumGhosts());
	int numCellsNG = blocks[ii].NumI() * blocks[ii].NumJ() * blocks[ii].NumK();

	//allocate vector data to appropriate size
	vector<primVars> primVecR(numCells);
	genArray initial(0.0);
	vector<genArray> residR(numCellsNG, initial);
	vector<double> dtR(numCellsNG);
	vector<double> waveR(numCellsNG);

	//unpack vector data into allocated vectors
	int position = 0;
	MPI_Unpack(recvBuffer, bufRecvData, &position, &primVecR[0], primVecR.size(), MPI_cellData, MPI_COMM_WORLD); //unpack states
	MPI_Unpack(recvBuffer, bufRecvData, &position, &residR[0], residR.size(), MPI_cellData, MPI_COMM_WORLD); //unpack residuals
	MPI_Unpack(recvBuffer, bufRecvData, &position, &dtR[0], dtR.size(), MPI_DOUBLE, MPI_COMM_WORLD); //unpack time steps
	MPI_Unpack(recvBuffer, bufRecvData, &position, &waveR[0], waveR.size(), MPI_DOUBLE, MPI_COMM_WORLD); //unpack average wave speeds

	//assign unpacked vector data to procBlock
	blocks[ii].SetStateVec(primVecR); //assign states
	blocks[ii].SetResidualVec(residR); //assign residuals
	blocks[ii].SetDtVec(dtR); //assign time steps
	blocks[ii].SetAvgWaveSpeedVec(waveR); //assign average wave speeds

	delete [] recvBuffer; //deallocate receiving buffer

      }
    }

  }
  
  //------------------------------------------------------------------------------------------------------------------------------------------------
  //                                                                NON - ROOT
  //------------------------------------------------------------------------------------------------------------------------------------------------
  else { // pack and send data (non-root)
    for ( unsigned int ii = 0; ii < localBlocks.size(); ii++ ){

      //get copy of vector data that needs to be sent (private class data cannot be packed/sent)
      vector<primVars> primVecS = localBlocks[ii].StateVec(); 
      vector<genArray> residS = localBlocks[ii].ResidualVec();
      vector<double> dtS = localBlocks[ii].DtVec();
      vector<double> waveS = localBlocks[ii].AvgWaveSpeedVec();

      //determine size of buffer to send
      int sendBufSize = 0;
      int tempSize = 0;
      MPI_Pack_size(primVecS.size(), MPI_cellData, MPI_COMM_WORLD, &tempSize); //add size for states
      sendBufSize += tempSize;
      MPI_Pack_size(residS.size(), MPI_cellData, MPI_COMM_WORLD, &tempSize); //add size for residuals
      sendBufSize += tempSize;
      MPI_Pack_size(dtS.size(), MPI_DOUBLE, MPI_COMM_WORLD, &tempSize); //add size for time steps
      sendBufSize += tempSize;
      MPI_Pack_size(waveS.size(), MPI_DOUBLE, MPI_COMM_WORLD, &tempSize); //add size for average wave speed
      sendBufSize += tempSize;

      char *sendBuffer = new char[sendBufSize]; //allocate buffer to pack data into

      //pack data to send into buffer
      int position = 0;
      MPI_Pack(&primVecS[0], primVecS.size(), MPI_cellData, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
      MPI_Pack(&residS[0], residS.size(), MPI_cellData, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
      MPI_Pack(&dtS[0], dtS.size(), MPI_DOUBLE, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);
      MPI_Pack(&waveS[0], waveS.size(), MPI_DOUBLE, sendBuffer, sendBufSize, &position, MPI_COMM_WORLD);

      //send buffer to appropriate processor
      MPI_Send(sendBuffer, sendBufSize, MPI_PACKED, ROOT, localBlocks[ii].GlobalPos(), MPI_COMM_WORLD);

      delete [] sendBuffer; //deallocate buffer

    }
  }

}

/*function to broadcast a string from ROOT to all processors. This is needed because it is not garunteed in the MPI standard that the commmand
line arguments will be on any processor but ROOT.  */
void BroadcastString( string &str ){
  // str -- string to broadcast to all processors

  int strSize = str.size();  //get size of string
  char *buf = new char[strSize]; //allcate a char buffer of string size
  strcpy(buf, str.c_str()); //copy string into buffer

  MPI_Bcast(&strSize, 1, MPI_INT, ROOT, MPI_COMM_WORLD);  //broadcast string size
  MPI_Bcast(&buf[0], strSize, MPI_CHAR, ROOT, MPI_COMM_WORLD);  //broadcast string as char

  //create new string and assign to old string
  string newStr(buf, strSize);
  str = newStr;

  delete [] buf; //deallocate buffer
}

/* Function to calculate the maximum of two resid instances and allow access to all the data in the resid instance. This is used to create an operation
for MPI_Reduce.
*/
void MaxLinf( resid *in, resid *inout, int *len, MPI_Datatype *MPI_DOUBLE_5INT){
  // *in -- pointer to all input residuals (from all procs)
  // *inout -- pointer to input and output residuals. The answer is stored here
  // *len -- pointer to array size of *in and *inout
  // *MPI_DOUBLE_5INT -- pointer to MPI_Datatype of double followed by 5 Ints, which represents the resid class

  resid resLinf; //intialize a resid

  for ( int ii = 0; ii < *len; ii++ ){ //loop over array of resids

    if ( in->linf >= inout->linf ){ //if linf from input is greater than or equal to linf from output, then new max has been found
      resLinf = *in;
    }
    else{ //no new max
      resLinf = *inout;
    }

    *inout = resLinf; //assign max to output

    //increment to next entry in array
    in++;
    inout++;
  }

}