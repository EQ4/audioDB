/* audioDB.cpp

audioDB version 1.0

A feature vector database management system for content-based retrieval.

Usage: audioDB [OPTIONS]...

      --full-help              Print help, including hidden options, and exit
  -V, --version                Print version and exit
  -H, --help                   print help on audioDB usage and exit.
  -v, --verbosity=detail       level of detail of operational information.  
                                 (default=`1')

Database Setup:
  All database operations require a database argument.
  
  Database commands are UPPER CASE. Command options are lower case.

  -d, --database=filename      database file required by Database commands.
  -N, --NEW                    make a new (initially empty) database.
  -S, --STATUS                 output database information to stdout.
  -D, --DUMP                   output all entries: index key size.
  -L, --L2NORM                 unit norm vectors and norm all future inserts.

Database Insertion:
  The following commands insert feature files, with optional keys and 
  timestamps.

  -I, --INSERT                 add feature vectors to an existing database.
  -U, --UPDATE                 replace inserted vectors associated with key 
                                 with new input vectors.
  -f, --features=filename      binary series of vectors file {int sz:ieee 
                                 double[][sz]:eof}.
  -t, --times=filename         list of time points (ascii) for feature vectors.
  -k, --key=identifier         unique identifier associated with features.
  
  -B, --BATCHINSERT            add feature vectors named in a --featureList 
                                 file (with optional keys in a --keyList file) 
                                 to the named database.
  -F, --featureList=filename   text file containing list of binary feature 
                                 vector files to process
  -T, --timesList=filename     text file containing list of ascii --times for 
                                 each --features file in --featureList.
  -K, --keyList=filename       text file containing list of unique identifiers 
                                 associated with --features.

Database Search:
  Thse commands control the retrieval behaviour.

  -Q, --QUERY=searchtype       content-based search on --database using 
                                 --features as a query. Optionally restrict the 
                                 search to those segments identified in a 
                                 --keyList.  (possible values="point", 
                                 "segment", "sequence")
  -p, --qpoint=position        ordinal position of query start point in 
                                 --features file.  (default=`0')
  -e, --exhaustive             exhaustive search: iterate through all query 
                                 vectors in search. Overrides --qpoint.  
                                 (default=off)
  -n, --pointnn=numpoints      number of point nearest neighbours to use in 
                                 retrieval.  (default=`10')
  -R, --radius=DOUBLE          radius search, returns all 
                                 points/segments/sequences inside given radius. 
                                  (default=`1.0')
  -x, --expandfactor=DOUBLE    time compress/expand factor of result length to 
                                 query length [1.0 .. 100.0].  (default=`1.1')
  -o, --rotate                 rotate query vectors for rotationally invariant 
                                 search.  (default=off)
  -r, --resultlength=length    maximum length of the result list.  
                                 (default=`10')
  -l, --sequencelength=length  length of sequences for sequence search.  
                                 (default=`16')
  -h, --sequencehop=hop        hop size of sequence window for sequence search. 
                                  (default=`1')

Web Services:
  These commands enable the database process to establish a connection via the 
  internet and operate as separate client and server processes.

  -s, --SERVER=port            run as standalone web service on named port.  
                                 (default=`80011')
  -c, --client=hostname:port   run as a client using named host service.
  
  Copyright (C) 2007 Michael Casey, Goldsmiths, University of London
  
  outputs:
  
  key1 distance1 qpos1 spos1
  key2 distance2 qpos2 spos2
  ...
  keyN distanceN qposN sposN
  
*/

#include "audioDB.h"

#define O2_DEBUG

  void audioDB::error(const char* a, const char* b){
    cerr << a << ":" << b << endl;
    exit(1);
}

audioDB::audioDB(const unsigned argc, char* const argv[], adb__queryResult *adbQueryResult):
  dim(0),
  dbName(0),
  inFile(0),
  key(0),
  segFile(0),
  segFileName(0),
  timesFile(0),
  timesFileName(0),
  usingTimes(0),
  command(0),
  dbfid(0),
  db(0),
  dbH(0),
  infid(0),
  indata(0),
  queryType(O2_FLAG_POINT_QUERY),
  verbosity(1),
  pointNN(O2_DEFAULT_POINTNN),
  segNN(O2_DEFAULT_SEGNN),
  segTable(0),
  fileTable(0),
  dataBuf(0),
  l2normTable(0),
  timesTable(0),
  qNorm(0),
  sequenceLength(16),
  sequenceHop(1),
  queryPoint(0),
  usingQueryPoint(0),
  isClient(0),
  isServer(0),
  port(0),
  timesTol(0.1){
  
  if(processArgs(argc, argv)<0){
    printf("No command found.\n");
    cmdline_parser_print_version ();
    if (strlen(gengetopt_args_info_purpose) > 0)
      printf("%s\n", gengetopt_args_info_purpose);
    printf("%s\n", gengetopt_args_info_usage);
    printf("%s\n", gengetopt_args_info_help[1]);
    printf("%s\n", gengetopt_args_info_help[2]);
    printf("%s\n", gengetopt_args_info_help[0]);
    exit(1);
  }
  
  if(O2_ACTION(COM_SERVER))
    startServer();

  else  if(O2_ACTION(COM_CREATE))
    create(dbName);

  else if(O2_ACTION(COM_INSERT))
    insert(dbName, inFile);

  else if(O2_ACTION(COM_BATCHINSERT))
    batchinsert(dbName, inFile);

  else if(O2_ACTION(COM_QUERY))
    if(isClient)
      ws_query(dbName, inFile, (char*)hostport);
    else
      query(dbName, inFile, adbQueryResult);

  else if(O2_ACTION(COM_STATUS))
    if(isClient)
      ws_status(dbName,(char*)hostport);
    else
      status(dbName);
  
  else if(O2_ACTION(COM_L2NORM))
    l2norm(dbName);
  
  else if(O2_ACTION(COM_DUMP))
    dump(dbName);
  
  else
    error("Unrecognized command",command);
}

audioDB::~audioDB(){
  // Clean up
  if(indata)
    munmap(indata,statbuf.st_size);
  if(db)
    munmap(db,O2_DEFAULTDBSIZE);
  if(dbfid>0)
    close(dbfid);
  if(infid>0)
    close(infid);
  if(dbH)
    delete dbH;
}

int audioDB::processArgs(const unsigned argc, char* const argv[]){

  if(argc<2){
    cmdline_parser_print_version ();
    if (strlen(gengetopt_args_info_purpose) > 0)
      printf("%s\n", gengetopt_args_info_purpose);
    printf("%s\n", gengetopt_args_info_usage);
    printf("%s\n", gengetopt_args_info_help[1]);
    printf("%s\n", gengetopt_args_info_help[2]);
    printf("%s\n", gengetopt_args_info_help[0]);
    exit(0);
  }

  if (cmdline_parser (argc, argv, &args_info) != 0)
    exit(1) ;       

  if(args_info.help_given){
    cmdline_parser_print_help();
    exit(0);
  }

  if(args_info.verbosity_given){
    verbosity=args_info.verbosity_arg;
    if(verbosity<0 || verbosity>10){
      cerr << "Warning: verbosity out of range, setting to 1" << endl;
      verbosity=1;
    }
  }

  if(args_info.SERVER_given){
    command=COM_SERVER;
    port=args_info.SERVER_arg;
    if(port<100 || port > 100000)
      error("port out of range");
    isServer=1;
    return 0;
  }

  // No return on client command, find database command
 if(args_info.client_given){
   command=COM_CLIENT;
   hostport=args_info.client_arg;
   isClient=1;
 }

 if(args_info.NEW_given){
   command=COM_CREATE;
   dbName=args_info.database_arg;
   return 0;
 }

 if(args_info.STATUS_given){
   command=COM_STATUS;
   dbName=args_info.database_arg;
   return 0;
 }

 if(args_info.DUMP_given){
   command=COM_DUMP;
   dbName=args_info.database_arg;
   return 0;
 }

 if(args_info.L2NORM_given){
   command=COM_L2NORM;
   dbName=args_info.database_arg;
   return 0;
 }
       
 if(args_info.INSERT_given){
   command=COM_INSERT;
   dbName=args_info.database_arg;
   inFile=args_info.features_arg;
   if(args_info.key_given)
     key=args_info.key_arg;
   if(args_info.times_given){
     timesFileName=args_info.times_arg;
     if(strlen(timesFileName)>0){
       if(!(timesFile = new ifstream(timesFileName,ios::in)))
	 error("Could not open times file for reading", timesFileName);
       usingTimes=1;
     }
   }
   return 0;
 }
 
 if(args_info.BATCHINSERT_given){
   command=COM_BATCHINSERT;
   dbName=args_info.database_arg;
   inFile=args_info.featureList_arg;
   if(args_info.keyList_given)
     key=args_info.keyList_arg; // INCONSISTENT NO CHECK

   /* TO DO: REPLACE WITH
      if(args_info.keyList_given){
      segFileName=args_info.keyList_arg;
      if(strlen(segFileName)>0 && !(segFile = new ifstream(segFileName,ios::in)))
      error("Could not open keyList file for reading",segFileName);
      }
      AND UPDATE BATCHINSERT()
   */
   
   if(args_info.timesList_given){
     timesFileName=args_info.timesList_arg;
     if(strlen(timesFileName)>0){
       if(!(timesFile = new ifstream(timesFileName,ios::in)))
	 error("Could not open timesList file for reading", timesFileName);
       usingTimes=1;
     }
   }
   return 0;
 }

 // Query command and arguments
 if(args_info.QUERY_given){
   command=COM_QUERY;
   dbName=args_info.database_arg;
   inFile=args_info.features_arg;

   if(args_info.keyList_given){
     segFileName=args_info.keyList_arg;
     if(strlen(segFileName)>0 && !(segFile = new ifstream(segFileName,ios::in)))
       error("Could not open keyList file for reading",segFileName);
   }

   if(args_info.times_given){
     timesFileName=args_info.times_arg;
     if(strlen(timesFileName)>0){
       if(!(timesFile = new ifstream(timesFileName,ios::in)))
	 error("Could not open times file for reading", timesFileName);
       usingTimes=1;
     }
   }

   // query type
   if(strncmp(args_info.QUERY_arg, "segment", MAXSTR)==0)
     queryType=O2_FLAG_SEG_QUERY;
   else if(strncmp(args_info.QUERY_arg, "point", MAXSTR)==0)
     queryType=O2_FLAG_POINT_QUERY;
   else if(strncmp(args_info.QUERY_arg, "sequence", MAXSTR)==0)
     queryType=O2_FLAG_SEQUENCE_QUERY;
   else
     error("unsupported query type",args_info.QUERY_arg);

   if(!args_info.exhaustive_flag){
     queryPoint = args_info.qpoint_arg;
     usingQueryPoint=1;
     if(queryPoint<0 || queryPoint >10000)
       error("queryPoint out of range: 0 <= queryPoint <= 10000");
   }


   pointNN=args_info.pointnn_arg;
   if(pointNN<1 || pointNN >1000)
     error("pointNN out of range: 1 <= pointNN <= 1000");

   

   segNN=args_info.resultlength_arg;
   if(segNN<1 || segNN >1000)
     error("resultlength out of range: 1 <= resultlength <= 1000");

	         
   sequenceLength=args_info.sequencelength_arg;
   if(sequenceLength<1 || sequenceLength >1000)
     error("seqlen out of range: 1 <= seqlen <= 1000");

   sequenceHop=args_info.sequencehop_arg;
   if(sequenceHop<1 || sequenceHop >1000)
     error("seqhop out of range: 1 <= seqhop <= 1000");

   return 0;
 }
 return -1; // no command found
}

/* Make a new database

   The database consists of:

   header
   ---------------------------------------------------------------------------------
   | magic 4 bytes| numFiles 4 bytes | dim 4 bytes | length 4 bytes |flags 4 bytes |
   ---------------------------------------------------------------------------------
   

   keyTable : list of keys of segments
   --------------------------------------------------------------------------
   | key 256 bytes                                                          |
   --------------------------------------------------------------------------
   O2_MAXFILES*02_FILENAMELENGTH

   segTable : Maps implicit feature index to a feature vector matrix
   --------------------------------------------------------------------------
   | numVectors (4 bytes)                                                   |
   --------------------------------------------------------------------------
   O2_MAXFILES * 02_MEANNUMFEATURES * sizeof(INT)

   featureTable
   --------------------------------------------------------------------------
   | v1 v2 v3 ... vd (double)                                               |
   --------------------------------------------------------------------------
   O2_MAXFILES * 02_MEANNUMFEATURES * DIM * sizeof(DOUBLE)

   timesTable
   --------------------------------------------------------------------------
   | timestamp (double)                                                     |
   --------------------------------------------------------------------------
   O2_MAXFILES * 02_MEANNUMFEATURES * sizeof(DOUBLE)

   l2normTable
   --------------------------------------------------------------------------
   | nm (double)                                                            |
   --------------------------------------------------------------------------
   O2_MAXFILES * 02_MEANNUMFEATURES * sizeof(DOUBLE)

*/

void audioDB::create(const char* dbName){
  if ((dbfid = open (dbName, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)) < 0)
    error("Can't open database file", dbName);

  // go to the location corresponding to the last byte
  if (lseek (dbfid, O2_DEFAULTDBSIZE - 1, SEEK_SET) == -1)
    error("lseek error in db file");

  // write a dummy byte at the last location
  if (write (dbfid, "", 1) != 1)
    error("write error");
  
  // mmap the output file
  if(verbosity)
    cerr << "header size:" << O2_HEADERSIZE << endl;
  if ((db = (char*) mmap(0, O2_DEFAULTDBSIZE, PROT_READ | PROT_WRITE,
			 MAP_SHARED, dbfid, 0)) == (caddr_t) -1)
    error("mmap error for creating database");
  
  dbH = new dbTableHeaderT();
  assert(dbH);

  // Initialize header
  dbH->magic=O2_MAGIC;
  dbH->numFiles=0;
  dbH->length=0;
  dbH->dim=0;
  dbH->flags=0; //O2_FLAG_L2NORM;

  memcpy (db, dbH, O2_HEADERSIZE);
  if(verbosity)
    cerr << COM_CREATE << " " << dbName << endl;

}


void audioDB::drop(){
    
    
}

// initTables - memory map files passed as arguments
// Precondition: database has already been created
void audioDB::initTables(const char* dbName, const char* inFile=0){
  if ((dbfid = open (dbName, O_RDWR)) < 0)
    error("Can't open database file:", dbName);
  
  // open the input file
  if (inFile && (infid = open (inFile, O_RDONLY)) < 0)
    error("can't open input file for reading", inFile);

  // find size of input file
  if (inFile && fstat (infid,&statbuf) < 0)
    error("fstat error finding size of input");
  
  // Get the database header info
  dbH = new dbTableHeaderT();
  assert(dbH);
  
  if(read(dbfid,(char*)dbH,sizeof(dbTableHeaderT))!=sizeof(dbTableHeaderT))
    error("error reading db header");

  fileTableOffset = O2_HEADERSIZE;
  segTableOffset = fileTableOffset + O2_FILETABLESIZE*O2_MAXFILES;
  dataoffset = segTableOffset + O2_SEGTABLESIZE*O2_MAXFILES;
  l2normTableOffset = O2_DEFAULTDBSIZE - O2_MAXFILES*O2_MEANNUMVECTORS*sizeof(double);
  timesTableOffset = l2normTableOffset - O2_MAXFILES*O2_MEANNUMVECTORS*sizeof(double);

  if(dbH->magic!=O2_MAGIC){
    cerr << "expected: " << O2_MAGIC << ", got:" << dbH->magic << endl;
    error("database file has incorrect header",dbName);
  }

  if(inFile)
    if(dbH->dim==0 && dbH->length==0) // empty database
      read(infid,&dbH->dim,sizeof(unsigned)); // initialize with input dimensionality
    else {
      unsigned test;
      read(infid,&test,sizeof(unsigned));
      if(dbH->dim!=test){      
	cerr << "error: expected dimension: " << dbH->dim << ", got :" << test <<endl;
	error("feature dimensions do not match database table dimensions");
      }
    }
  
  // mmap the input file 
  if (inFile && (indata = (char*)mmap (0, statbuf.st_size, PROT_READ, MAP_SHARED, infid, 0))
      == (caddr_t) -1)
    error("mmap error for input");

  // mmap the database file
  if ((db = (char*) mmap(0, O2_DEFAULTDBSIZE, PROT_READ | PROT_WRITE,
			 MAP_SHARED, dbfid, 0)) == (caddr_t) -1)
    error("mmap error for creating database");

  // Make some handy tables with correct types
  fileTable= (char*)(db+fileTableOffset);
  segTable = (unsigned*)(db+segTableOffset);
  dataBuf  = (double*)(db+dataoffset);
  l2normTable = (double*)(db+l2normTableOffset);
  timesTable = (double*)(db+timesTableOffset);

}

void audioDB::insert(const char* dbName, const char* inFile){

  initTables(dbName, inFile);

  if(!usingTimes && (dbH->flags & O2_FLAG_TIMES))
    error("Must use timestamps with timestamped database","use --times");

  // Check that there is room for at least 1 more file
  if((char*)timesTable<((char*)dataBuf+dbH->length+statbuf.st_size-sizeof(int)))
    error("No more room in database","insert failed: reason database is full.");
  
  if(!key)
    key=inFile;
  // Linear scan of filenames check for pre-existing feature
  unsigned alreadyInserted=0;
  for(unsigned k=0; k<dbH->numFiles; k++)
    if(strncmp(fileTable + k*O2_FILETABLESIZE, key, strlen(key))==0){
      alreadyInserted=1;
      break;
    }

  if(alreadyInserted){
    if(verbosity)
      cerr << "Warning: key already exists in database, ignoring: " <<inFile << endl;
    return;
  }
  
  // Make a segment index table of features to file indexes
  unsigned numVectors = (statbuf.st_size-sizeof(int))/(sizeof(double)*dbH->dim);
  if(!numVectors){
    if(verbosity)
      cerr << "Warning: ignoring zero-length feature vector file:" << key << endl;
    // CLEAN UP
    munmap(indata,statbuf.st_size);
    munmap(db,O2_DEFAULTDBSIZE);
    close(infid);
    return;
  }

  strncpy(fileTable + dbH->numFiles*O2_FILETABLESIZE, key, strlen(key));

  unsigned insertoffset = dbH->length;// Store current state

  // Check times status and insert times from file
  unsigned timesoffset=insertoffset/(dbH->dim*sizeof(double));
  double* timesdata=timesTable+timesoffset;
  assert(timesdata+numVectors<l2normTable);
  insertTimeStamps(numVectors, timesFile, timesdata);

  // Increment file count
  dbH->numFiles++;

  // Update Header information
  dbH->length+=(statbuf.st_size-sizeof(int));

  // Copy the header back to the database
  memcpy (db, dbH, sizeof(dbTableHeaderT));  

  // Update segment to file index map
  //memcpy (db+segTableOffset+(dbH->numFiles-1)*sizeof(unsigned), &numVectors, sizeof(unsigned));  
  memcpy (segTable+dbH->numFiles-1, &numVectors, sizeof(unsigned));  

  // Update the feature database
  memcpy (db+dataoffset+insertoffset, indata+sizeof(int), statbuf.st_size-sizeof(int));
  
  // Norm the vectors on input if the database is already L2 normed
  if(dbH->flags & O2_FLAG_L2NORM)
    unitNormAndInsertL2((double*)(db+dataoffset+insertoffset), dbH->dim, numVectors, 1); // append

  // Report status
  status(dbName);
  if(verbosity)
    cerr << COM_INSERT << " " << dbName << " " << numVectors << " vectors " 
	 << (statbuf.st_size-sizeof(int)) << " bytes." << endl;

  // CLEAN UP
  munmap(indata,statbuf.st_size);
  close(infid);
}

void audioDB::insertTimeStamps(unsigned numVectors, ifstream* timesFile, double* timesdata){
  unsigned numtimes=0;
 if(usingTimes){
   if(!(dbH->flags & O2_FLAG_TIMES) && !dbH->numFiles)
     dbH->flags=dbH->flags|O2_FLAG_TIMES;
   else if(!(dbH->flags&O2_FLAG_TIMES)){
     cerr << "Warning: timestamp file used with non time-stamped database: ignoring timestamps" << endl;
     usingTimes=0;
   }
   
   if(!timesFile->is_open()){
     if(dbH->flags & O2_FLAG_TIMES){
       munmap(indata,statbuf.st_size);
       munmap(db,O2_DEFAULTDBSIZE);
       error("problem opening times file on timestamped database",timesFileName);
     }
     else{
       cerr << "Warning: problem opening times file. But non-timestamped database, so ignoring times file." << endl;
       usingTimes=0;
     }      
   }

    // Process time file
   if(usingTimes){
     do{
       *timesFile>>*timesdata++;
       if(timesFile->eof())
	 break;
       numtimes++;
     }while(!timesFile->eof() && numtimes<numVectors);
     if(!timesFile->eof()){
	double dummy;
	do{
	  *timesFile>>dummy;
	  if(timesFile->eof())
	    break;
	  numtimes++;
	}while(!timesFile->eof());
     }
     if(numtimes<numVectors || numtimes>numVectors+2){
       munmap(indata,statbuf.st_size);
       munmap(db,O2_DEFAULTDBSIZE);
       close(infid);
       cerr << "expected " << numVectors << " found " << numtimes << endl;
       error("Times file is incorrect length for features file",inFile);
     }
     if(verbosity>2)
       cerr << "numtimes: " << numtimes << endl;
   }
 }
}

void audioDB::batchinsert(const char* dbName, const char* inFile){

  if ((dbfid = open (dbName, O_RDWR)) < 0)
    error("Can't open database file:", dbName);

  if(!key)
    key=inFile;
  ifstream *filesIn = 0;
  ifstream *keysIn = 0;
  ifstream* thisTimesFile = 0;

  if(!(filesIn = new ifstream(inFile)))
    error("Could not open batch in file", inFile);
  if(key && key!=inFile)
    if(!(keysIn = new ifstream(key)))
      error("Could not open batch key file",key);
  
  // Get the database header info
  dbH = new dbTableHeaderT();
  assert(dbH);
  
  if(read(dbfid,(char*)dbH,sizeof(dbTableHeaderT))!=sizeof(dbTableHeaderT))
    error("error reading db header");

  if(!usingTimes && (dbH->flags & O2_FLAG_TIMES))
    error("Must use timestamps with timestamped database","use --times");

  fileTableOffset = O2_HEADERSIZE;
  segTableOffset = fileTableOffset + O2_FILETABLESIZE*O2_MAXFILES;
  dataoffset = segTableOffset + O2_SEGTABLESIZE*O2_MAXFILES;
  l2normTableOffset = O2_DEFAULTDBSIZE - O2_MAXFILES*O2_MEANNUMVECTORS*sizeof(double);
  timesTableOffset = l2normTableOffset - O2_MAXFILES*O2_MEANNUMVECTORS*sizeof(double);

  if(dbH->magic!=O2_MAGIC){
    cerr << "expected:" << O2_MAGIC << ", got:" << dbH->magic << endl;
    error("database file has incorrect header",dbName);
  }

  
  unsigned totalVectors=0;
  char *thisKey = new char[MAXSTR];
  char *thisFile = new char[MAXSTR];
  char *thisTimesFileName = new char[MAXSTR];
    
  do{
    filesIn->getline(thisFile,MAXSTR);
    if(key && key!=inFile)
      keysIn->getline(thisKey,MAXSTR);
    else
      thisKey = thisFile;
    if(usingTimes)
      timesFile->getline(thisTimesFileName,MAXSTR);	  
    
    if(filesIn->eof())
      break;

    // open the input file
    if (thisFile && (infid = open (thisFile, O_RDONLY)) < 0)
      error("can't open feature file for reading", thisFile);
  
    // find size of input file
    if (thisFile && fstat (infid,&statbuf) < 0)
      error("fstat error finding size of input");

    // mmap the database file
    if ((db = (char*) mmap(0, O2_DEFAULTDBSIZE, PROT_READ | PROT_WRITE,
			   MAP_SHARED, dbfid, 0)) == (caddr_t) -1)
      error("mmap error for creating database");
    
    // Make some handy tables with correct types
    fileTable= (char*)(db+fileTableOffset);
    segTable = (unsigned*)(db+segTableOffset);
    dataBuf  = (double*)(db+dataoffset);
    l2normTable = (double*)(db+l2normTableOffset);
    timesTable = (double*)(db+timesTableOffset);

    // Check that there is room for at least 1 more file
    if((char*)timesTable<((char*)dataBuf+(dbH->length+statbuf.st_size-sizeof(int))))
      error("No more room in database","insert failed: reason database is full.");
    
    if(thisFile)
      if(dbH->dim==0 && dbH->length==0) // empty database
	read(infid,&dbH->dim,sizeof(unsigned)); // initialize with input dimensionality
      else {
	unsigned test;
	read(infid,&test,sizeof(unsigned));
	if(dbH->dim!=test){      
	  cerr << "error: expected dimension: " << dbH->dim << ", got :" << test <<endl;
	  error("feature dimensions do not match database table dimensions");
	}
      }
  
    // mmap the input file 
    if (thisFile && (indata = (char*)mmap (0, statbuf.st_size, PROT_READ, MAP_SHARED, infid, 0))
	== (caddr_t) -1)
      error("mmap error for input");
  
  
    // Linear scan of filenames check for pre-existing feature
    unsigned alreadyInserted=0;
  
    for(unsigned k=0; k<dbH->numFiles; k++)
      if(strncmp(fileTable + k*O2_FILETABLESIZE, thisKey, strlen(thisKey))==0){
	alreadyInserted=1;
	break;
      }
  
    if(alreadyInserted){
      if(verbosity)
	cerr << "Warning: key already exists in database:" << thisKey << endl;
    }
    else{
  
      // Make a segment index table of features to file indexes
      unsigned numVectors = (statbuf.st_size-sizeof(int))/(sizeof(double)*dbH->dim);
      if(!numVectors){
	if(verbosity)
	  cerr << "Warning: ignoring zero-length feature vector file:" << thisKey << endl;
      }
      else{	
	if(usingTimes){
	  if(timesFile->eof())
	    error("not enough timestamp files in timesList");
	  thisTimesFile=new ifstream(thisTimesFileName,ios::in);
	  if(!thisTimesFile->is_open())
	    error("Cannot open timestamp file",thisTimesFileName);
	  unsigned insertoffset=dbH->length;
	  unsigned timesoffset=insertoffset/(dbH->dim*sizeof(double));
	  double* timesdata=timesTable+timesoffset;
	  assert(timesdata+numVectors<l2normTable);
	  insertTimeStamps(numVectors,thisTimesFile,timesdata);
	  if(thisTimesFile)
	    delete thisTimesFile;
	}
	  
	strncpy(fileTable + dbH->numFiles*O2_FILETABLESIZE, thisKey, strlen(thisKey));
  
	unsigned insertoffset = dbH->length;// Store current state

	// Increment file count
	dbH->numFiles++;  
  
	// Update Header information
	dbH->length+=(statbuf.st_size-sizeof(int));
	// Copy the header back to the database
	memcpy (db, dbH, sizeof(dbTableHeaderT));  
  
	// Update segment to file index map
	//memcpy (db+segTableOffset+(dbH->numFiles-1)*sizeof(unsigned), &numVectors, sizeof(unsigned));  
	memcpy (segTable+dbH->numFiles-1, &numVectors, sizeof(unsigned));  
	
	// Update the feature database
	memcpy (db+dataoffset+insertoffset, indata+sizeof(int), statbuf.st_size-sizeof(int));
	
	// Norm the vectors on input if the database is already L2 normed
	if(dbH->flags & O2_FLAG_L2NORM)
	  unitNormAndInsertL2((double*)(db+dataoffset+insertoffset), dbH->dim, numVectors, 1); // append
	
	totalVectors+=numVectors;
      }
    }
    // CLEAN UP
    munmap(indata,statbuf.st_size);
    close(infid);
    munmap(db,O2_DEFAULTDBSIZE);
  }while(!filesIn->eof());

  // mmap the database file
  if ((db = (char*) mmap(0, O2_DEFAULTDBSIZE, PROT_READ | PROT_WRITE,
			 MAP_SHARED, dbfid, 0)) == (caddr_t) -1)
    error("mmap error for creating database");
  
  if(verbosity)
    cerr << COM_BATCHINSERT << " " << dbName << " " << totalVectors << " vectors " 
	 << totalVectors*dbH->dim*sizeof(double) << " bytes." << endl;
  
  // Report status
  status(dbName);
  
  munmap(db,O2_DEFAULTDBSIZE);
}

void audioDB::ws_status(const char*dbName, char* hostport){
  struct soap soap;
  int adbStatusResult;  
  
  // Query an existing adb database
  soap_init(&soap);
  if(soap_call_adb__status(&soap,hostport,NULL,(char*)dbName,adbStatusResult)==SOAP_OK)
    std::cout << "result = " << adbStatusResult << std::endl;
  else
    soap_print_fault(&soap,stderr);
  
  soap_destroy(&soap);
  soap_end(&soap);
  soap_done(&soap);
}

void audioDB::ws_query(const char*dbName, const char *segKey, const char* hostport){
  struct soap soap;
  adb__queryResult adbQueryResult;  

  soap_init(&soap);  
  if(soap_call_adb__query(&soap,hostport,NULL,
			  (char*)dbName,(char*)segKey,(char*)segFileName,(char*)timesFileName,
			  queryType, queryPoint, pointNN, segNN, sequenceLength, adbQueryResult)==SOAP_OK){
    //std::cerr << "result list length:" << adbQueryResult.__sizeRlist << std::endl;
    for(int i=0; i<adbQueryResult.__sizeRlist; i++)
      std::cout << adbQueryResult.Rlist[i] << " " << adbQueryResult.Dist[i] 
		<< " " << adbQueryResult.Qpos[i] << " " << adbQueryResult.Spos[i] << std::endl;
  }
  else
    soap_print_fault(&soap,stderr);
  
  soap_destroy(&soap);
  soap_end(&soap);
  soap_done(&soap);

}


void audioDB::status(const char* dbName){
  if(!dbH)
    initTables(dbName, 0);
  
  // Update Header information
  cout << "num files:" << dbH->numFiles << endl;
  cout << "data dim:" << dbH->dim <<endl;
  if(dbH->dim>0){
    cout << "total vectors:" << dbH->length/(sizeof(double)*dbH->dim)<<endl;
    cout << "vectors available:" << (timesTableOffset-(dataoffset+dbH->length))/(sizeof(double)*dbH->dim) << endl;
  }
  cout << "total bytes:" << dbH->length << " (" << (100.0*dbH->length)/(timesTableOffset-dataoffset) << "%)" << endl;
  cout << "bytes available:" << timesTableOffset-(dataoffset+dbH->length) << " (" <<
    (100.0*(timesTableOffset-(dataoffset+dbH->length)))/(timesTableOffset-dataoffset) << "%)" << endl;
  cout << "flags:" << dbH->flags << endl;

  unsigned dudCount=0;
  unsigned nullCount=0;
  for(unsigned k=0; k<dbH->numFiles; k++){
    if(segTable[k]<sequenceLength){
      dudCount++;
      if(!segTable[k])
	nullCount++;
    }
  }
  cout << "null count: " << nullCount << " small sequence count " << dudCount-nullCount << endl;    
}


void audioDB::dump(const char* dbName){
  if(!dbH)
    initTables(dbName,0);
  
  for(unsigned k=0; k<dbH->numFiles; k++)
    cout << fileTable+k*O2_FILETABLESIZE << " " << segTable[k] << endl;

  status(dbName);
}

void audioDB::l2norm(const char* dbName){
  initTables(dbName,0);
  if(dbH->length>0){
    unsigned numVectors = dbH->length/(sizeof(double)*dbH->dim);
    unitNormAndInsertL2(dataBuf, dbH->dim, numVectors, 0); // No append
  }
  // Update database flags
  dbH->flags = dbH->flags|O2_FLAG_L2NORM;
  memcpy (db, dbH, O2_HEADERSIZE);
}
  

  
void audioDB::query(const char* dbName, const char* inFile, adb__queryResult *adbQueryResult){  
  switch(queryType){
  case O2_FLAG_POINT_QUERY:
    pointQuery(dbName, inFile, adbQueryResult);
    break;
  case O2_FLAG_SEQUENCE_QUERY:
    segSequenceQuery(dbName, inFile, adbQueryResult);
    break;
  case O2_FLAG_SEG_QUERY:
    segPointQuery(dbName, inFile, adbQueryResult);
    break;
  default:
    error("unrecognized queryType in query()");
    
  }  
}

//return ordinal position of key in keyTable
unsigned audioDB::getKeyPos(char* key){  
  for(unsigned k=0; k<dbH->numFiles; k++)
    if(strncmp(fileTable + k*O2_FILETABLESIZE, key, strlen(key))==0)
      return k;
  error("Key not found",key);
  return O2_ERR_KEYNOTFOUND;
}

// Basic point query engine
void audioDB::pointQuery(const char* dbName, const char* inFile, adb__queryResult *adbQueryResult){
  
  initTables(dbName, inFile);
  
  // For each input vector, find the closest pointNN matching output vectors and report
  // we use stdout in this stub version
  unsigned numVectors = (statbuf.st_size-sizeof(int))/(sizeof(double)*dbH->dim);
    
  double* query = (double*)(indata+sizeof(int));
  double* data = dataBuf;
  double* queryCopy = 0;

  if( dbH->flags & O2_FLAG_L2NORM ){
    // Make a copy of the query
    queryCopy = new double[numVectors*dbH->dim];
    qNorm = new double[numVectors];
    assert(queryCopy&&qNorm);
    memcpy(queryCopy, query, numVectors*dbH->dim*sizeof(double));
    unitNorm(queryCopy, dbH->dim, numVectors, qNorm);
    query = queryCopy;
  }

  // Make temporary dynamic memory for results
  assert(pointNN>0 && pointNN<=O2_MAXNN);
  double distances[pointNN];
  unsigned qIndexes[pointNN];
  unsigned sIndexes[pointNN];
  for(unsigned k=0; k<pointNN; k++){
    distances[k]=0.0;
    qIndexes[k]=~0;
    sIndexes[k]=~0;    
  }

  unsigned j=numVectors; 
  unsigned k,l,n;
  double thisDist;

  unsigned totalVecs=dbH->length/(dbH->dim*sizeof(double));
  double meanQdur = 0;
  double* timesdata = 0;
  double* dbdurs = 0;

  if(usingTimes && !(dbH->flags & O2_FLAG_TIMES)){
    cerr << "warning: ignoring query timestamps for non-timestamped database" << endl;
    usingTimes=0;
  }

  else if(!usingTimes && (dbH->flags & O2_FLAG_TIMES))
    cerr << "warning: no timestamps given for query. Ignoring database timestamps." << endl;
  
  else if(usingTimes && (dbH->flags & O2_FLAG_TIMES)){
    timesdata = new double[numVectors];
    insertTimeStamps(numVectors, timesFile, timesdata);
    // Calculate durations of points
    for(k=0; k<numVectors-1; k++){
      timesdata[k]=timesdata[k+1]-timesdata[k];
      meanQdur+=timesdata[k];
    }
    meanQdur/=k;
    // Individual exhaustive timepoint durations
    dbdurs = new double[totalVecs];
    for(k=0; k<totalVecs-1; k++)
      dbdurs[k]=timesTable[k+1]-timesTable[k];
    j--; // decrement vector counter by one
  }

  if(usingQueryPoint)
    if(queryPoint>numVectors-1)
      error("queryPoint > numVectors in query");
    else{
      if(verbosity>1)
	cerr << "query point: " << queryPoint << endl; cerr.flush();
      query=query+queryPoint*dbH->dim;
      numVectors=queryPoint+1;
      j=1;
    }

  gettimeofday(&tv1, NULL);   
  while(j--){ // query
    data=dataBuf;
    k=totalVecs; // number of database vectors
    while(k--){  // database
      thisDist=0;
      l=dbH->dim;
      double* q=query;
      while(l--)
	thisDist+=*q++**data++;
      if(!usingTimes || 
	 (usingTimes 
	  && fabs(dbdurs[totalVecs-k-1]-timesdata[numVectors-j-1])<timesdata[numVectors-j-1]*timesTol)){
	n=pointNN;
	while(n--){
	  if(thisDist>=distances[n]){
	    if((n==0 || thisDist<=distances[n-1])){
	      // Copy all values above up the queue
	      for( l=pointNN-1 ; l >= n+1 ; l--){
		distances[l]=distances[l-1];
		qIndexes[l]=qIndexes[l-1];
		sIndexes[l]=sIndexes[l-1];	      
	      }
	      distances[n]=thisDist;
	      qIndexes[n]=numVectors-j-1;
	      sIndexes[n]=dbH->length/(sizeof(double)*dbH->dim)-k-1;
	      break;
	    }
	  }
	  else
	    break;
	}
      }
    }
    // Move query pointer to next query point
    query+=dbH->dim;
  }

  gettimeofday(&tv2, NULL); 
  if(verbosity>1)
    cerr << endl << " elapsed time:" << ( tv2.tv_sec*1000 + tv2.tv_usec/1000 ) - ( tv1.tv_sec*1000+tv1.tv_usec/1000 ) << " msec" << endl;

  if(adbQueryResult==0){
    // Output answer
    // Loop over nearest neighbours    
    for(k=0; k < pointNN; k++){
      // Scan for key
      unsigned cumSeg=0;
      for(l=0 ; l<dbH->numFiles; l++){
	cumSeg+=segTable[l];
	if(sIndexes[k]<cumSeg){
	  cout << fileTable+l*O2_FILETABLESIZE << " " << distances[k] << " " << qIndexes[k] << " " 
	       << sIndexes[k]+segTable[l]-cumSeg << endl;
	  break;
	}
      }
    }
  }
  else{ // Process Web Services Query
    int listLen = pointNN;
    adbQueryResult->__sizeRlist=listLen;
    adbQueryResult->__sizeDist=listLen;
    adbQueryResult->__sizeQpos=listLen;
    adbQueryResult->__sizeSpos=listLen;
    adbQueryResult->Rlist= new char*[listLen];
    adbQueryResult->Dist = new double[listLen];
    adbQueryResult->Qpos = new int[listLen];
    adbQueryResult->Spos = new int[listLen];
    for(k=0; k<adbQueryResult->__sizeRlist; k++){
      adbQueryResult->Rlist[k]=new char[O2_MAXFILESTR];
      adbQueryResult->Dist[k]=distances[k];
      adbQueryResult->Qpos[k]=qIndexes[k];
      unsigned cumSeg=0;
      for(l=0 ; l<dbH->numFiles; l++){
	cumSeg+=segTable[l];
	if(sIndexes[k]<cumSeg){
	  sprintf(adbQueryResult->Rlist[k], "%s", fileTable+l*O2_FILETABLESIZE);
	  break;
	}
      }
      adbQueryResult->Spos[k]=sIndexes[k]+segTable[l]-cumSeg;
    }
  }
  
  // Clean up
  if(queryCopy)
    delete queryCopy;
  if(qNorm)
    delete qNorm;
  if(timesdata)
    delete timesdata;
  if(dbdurs)
    delete dbdurs;
}

void audioDB::sequenceQuery(const char* dbName, const char* inFile, adb__queryResult *adbQueryResult){  

}

// segPointQuery  
// return the segNN closest segs to the query seg
// uses average of pointNN points per seg 
void audioDB::segPointQuery(const char* dbName, const char* inFile, adb__queryResult *adbQueryResult){  
  initTables(dbName, inFile);
  
  // For each input vector, find the closest pointNN matching output vectors and report
  unsigned numVectors = (statbuf.st_size-sizeof(int))/(sizeof(double)*dbH->dim);
  unsigned numSegs = dbH->numFiles;

  double* query = (double*)(indata+sizeof(int));
  double* data = dataBuf;
  double* queryCopy = 0;

  if( dbH->flags & O2_FLAG_L2NORM ){
    // Make a copy of the query
    queryCopy = new double[numVectors*dbH->dim];
    qNorm = new double[numVectors];
    assert(queryCopy&&qNorm);
    memcpy(queryCopy, query, numVectors*dbH->dim*sizeof(double));
    unitNorm(queryCopy, dbH->dim, numVectors, qNorm);
    query = queryCopy;
  }

  assert(pointNN>0 && pointNN<=O2_MAXNN);
  assert(segNN>0 && segNN<=O2_MAXNN);

  // Make temporary dynamic memory for results
  double segDistances[segNN];
  unsigned segIDs[segNN];
  unsigned segQIndexes[segNN];
  unsigned segSIndexes[segNN];

  double distances[pointNN];
  unsigned qIndexes[pointNN];
  unsigned sIndexes[pointNN];

  unsigned j=numVectors; // number of query points
  unsigned k,l,n, seg, segOffset=0, processedSegs=0;
  double thisDist;

  for(k=0; k<pointNN; k++){
    distances[k]=0.0;
    qIndexes[k]=~0;
    sIndexes[k]=~0;    
  }

  for(k=0; k<segNN; k++){
    segDistances[k]=0.0;
    segQIndexes[k]=~0;
    segSIndexes[k]=~0;
    segIDs[k]=~0;
  }

  double meanQdur = 0;
  double* timesdata = 0;
  double* meanDBdur = 0;
  
  if(usingTimes && !(dbH->flags & O2_FLAG_TIMES)){
    cerr << "warning: ignoring query timestamps for non-timestamped database" << endl;
    usingTimes=0;
  }
  
  else if(!usingTimes && (dbH->flags & O2_FLAG_TIMES))
    cerr << "warning: no timestamps given for query. Ignoring database timestamps." << endl;
  
  else if(usingTimes && (dbH->flags & O2_FLAG_TIMES)){
    timesdata = new double[numVectors];
    insertTimeStamps(numVectors, timesFile, timesdata);
    // Calculate durations of points
    for(k=0; k<numVectors-1; k++){
      timesdata[k]=timesdata[k+1]-timesdata[k];
      meanQdur+=timesdata[k];
    }
    meanQdur/=k;
    meanDBdur = new double[dbH->numFiles];
    for(k=0; k<dbH->numFiles; k++){
      meanDBdur[k]=0.0;
      for(j=0; j<segTable[k]-1 ; j++)
	meanDBdur[k]+=timesTable[j+1]-timesTable[j];
      meanDBdur[k]/=j;
    }
  }

  if(usingQueryPoint)
    if(queryPoint>numVectors-1)
      error("queryPoint > numVectors in query");
    else{
      if(verbosity>1)
	cerr << "query point: " << queryPoint << endl; cerr.flush();
      query=query+queryPoint*dbH->dim;
      numVectors=queryPoint+1;
    }
  
  // build segment offset table
  unsigned *segOffsetTable = new unsigned[dbH->numFiles];
  unsigned cumSeg=0;
  unsigned segIndexOffset;
  for(k=0; k<dbH->numFiles;k++){
    segOffsetTable[k]=cumSeg;
    cumSeg+=segTable[k]*dbH->dim;
  }

  char nextKey[MAXSTR];

  gettimeofday(&tv1, NULL); 
        
  for(seg=0 ; seg < dbH->numFiles ; seg++, processedSegs++){
    if(segFile){
      if(!segFile->eof()){
	//*segFile>>seg;
	segFile->getline(nextKey,MAXSTR);
	if(verbosity>3){
	  cerr << nextKey << endl;
	  cerr.flush();
	}
	seg=getKeyPos(nextKey);
      }
      else
	break;
    }
    segOffset=segOffsetTable[seg];     // numDoubles offset
    segIndexOffset=segOffset/dbH->dim; // numVectors offset
    if(verbosity>7)
      cerr << seg << "." << segOffset/(dbH->dim) << "." << segTable[seg] << " | ";cerr.flush();

    if(dbH->flags & O2_FLAG_L2NORM)
      usingQueryPoint?query=queryCopy+queryPoint*dbH->dim:query=queryCopy;
    else
      usingQueryPoint?query=(double*)(indata+sizeof(int))+queryPoint*dbH->dim:query=(double*)(indata+sizeof(int));
    if(usingQueryPoint)
      j=1;
    else
      j=numVectors;
    while(j--){
      k=segTable[seg];  // number of vectors in seg
      data=dataBuf+segOffset; // data for seg
      while(k--){
	thisDist=0;
	l=dbH->dim;
	double* q=query;
	while(l--)
	  thisDist+=*q++**data++;
	if(!usingTimes || 
	   (usingTimes 
	    && fabs(meanDBdur[seg]-meanQdur)<meanQdur*timesTol)){
	  n=pointNN;
	  while(n--){
	    if(thisDist>=distances[n]){
	      if((n==0 || thisDist<=distances[n-1])){
		// Copy all values above up the queue
		for( l=pointNN-1 ; l > n ; l--){
		  distances[l]=distances[l-1];
		  qIndexes[l]=qIndexes[l-1];
		  sIndexes[l]=sIndexes[l-1];	      
		}
		distances[n]=thisDist;
		qIndexes[n]=numVectors-j-1;
		sIndexes[n]=segTable[seg]-k-1;
		break;
	      }
	    }
	    else
	      break;
	  }
	}
      } // seg
      // Move query pointer to next query point
      query+=dbH->dim;
    } // query 
    // Take the average of this seg's distance
    // Test the seg distances
    thisDist=0;
    n=pointNN;
    while(n--)
      thisDist+=distances[pointNN-n-1];
    thisDist/=pointNN;
    n=segNN;
    while(n--){
      if(thisDist>=segDistances[n]){
	if((n==0 || thisDist<=segDistances[n-1])){
	  // Copy all values above up the queue
	  for( l=pointNN-1 ; l > n ; l--){
	    segDistances[l]=segDistances[l-1];
	    segQIndexes[l]=segQIndexes[l-1];
	    segSIndexes[l]=segSIndexes[l-1];
	    segIDs[l]=segIDs[l-1];
	  }
	  segDistances[n]=thisDist;
	  segQIndexes[n]=qIndexes[0];
	  segSIndexes[n]=sIndexes[0];
	  segIDs[n]=seg;
	  break;
	}
      }
      else
	break;
    }
    for(unsigned k=0; k<pointNN; k++){
      distances[k]=0.0;
      qIndexes[k]=~0;
      sIndexes[k]=~0;    
    }
  } // segs
  gettimeofday(&tv2, NULL); 

  if(verbosity>1)
    cerr << endl << "processed segs :" << processedSegs 
	 << " elapsed time:" << ( tv2.tv_sec*1000 + tv2.tv_usec/1000 ) - ( tv1.tv_sec*1000+tv1.tv_usec/1000 ) << " msec" << endl;

  if(adbQueryResult==0){
    if(verbosity>1)
      cerr<<endl;
    // Output answer
    // Loop over nearest neighbours
    for(k=0; k < min(segNN,processedSegs); k++)
      cout << fileTable+segIDs[k]*O2_FILETABLESIZE 
	   << " " << segDistances[k] << " " << segQIndexes[k] << " " << segSIndexes[k] << endl;
  }
  else{ // Process Web Services Query
    int listLen = min(segNN, processedSegs);
    adbQueryResult->__sizeRlist=listLen;
    adbQueryResult->__sizeDist=listLen;
    adbQueryResult->__sizeQpos=listLen;
    adbQueryResult->__sizeSpos=listLen;
    adbQueryResult->Rlist= new char*[listLen];
    adbQueryResult->Dist = new double[listLen];
    adbQueryResult->Qpos = new int[listLen];
    adbQueryResult->Spos = new int[listLen];
    for(k=0; k<adbQueryResult->__sizeRlist; k++){
      adbQueryResult->Rlist[k]=new char[O2_MAXFILESTR];
      adbQueryResult->Dist[k]=segDistances[k];
      adbQueryResult->Qpos[k]=segQIndexes[k];
      adbQueryResult->Spos[k]=segSIndexes[k];
      sprintf(adbQueryResult->Rlist[k], "%s", fileTable+segIDs[k]*O2_FILETABLESIZE);
    }
  }
    

  // Clean up
  if(segOffsetTable)
    delete segOffsetTable;
  if(queryCopy)
    delete queryCopy;
  if(qNorm)
    delete qNorm;
  if(timesdata)
    delete timesdata;
  if(meanDBdur)
    delete meanDBdur;

}
  
void audioDB::deleteDB(const char* dbName, const char* inFile){

}

// NBest matched filter distance between query and target segs
// efficient implementation
// outputs average of N minimum matched filter distances
void audioDB::segSequenceQuery(const char* dbName, const char* inFile, adb__queryResult *adbQueryResult){
  
  initTables(dbName, inFile);
  
  // For each input vector, find the closest pointNN matching output vectors and report
  // we use stdout in this stub version
  unsigned numVectors = (statbuf.st_size-sizeof(int))/(sizeof(double)*dbH->dim);
  unsigned numSegs = dbH->numFiles;
  
  double* query = (double*)(indata+sizeof(int));
  double* data = dataBuf;
  double* queryCopy = 0;

  double qMeanL2;
  double* sMeanL2;

  unsigned USE_THRESH=0;
  double SILENCE_THRESH=0;
  double DIFF_THRESH=0;

  if(!(dbH->flags & O2_FLAG_L2NORM) )
    error("Database must be L2 normed for sequence query","use -l2norm");
  
  if(verbosity>1)
    cerr << "performing norms ... "; cerr.flush();
  unsigned dbVectors = dbH->length/(sizeof(double)*dbH->dim);
  // Make a copy of the query
  queryCopy = new double[numVectors*dbH->dim];
  memcpy(queryCopy, query, numVectors*dbH->dim*sizeof(double));
  qNorm = new double[numVectors];
  sNorm = new double[dbVectors];
  sMeanL2=new double[dbH->numFiles];
  assert(qNorm&&sNorm&&queryCopy&&sMeanL2&&sequenceLength);    
  unitNorm(queryCopy, dbH->dim, numVectors, qNorm);
  query = queryCopy;
  // Make norm measurements relative to sequenceLength
  unsigned w = sequenceLength-1;
  unsigned i,j;
  double* ps;
  double tmp1,tmp2;
  // Copy the L2 norm values to core to avoid disk random access later on
  memcpy(sNorm, l2normTable, dbVectors*sizeof(double));
  double* snPtr = sNorm;
  for(i=0; i<dbH->numFiles; i++){
    if(segTable[i]>sequenceLength){
      tmp1=*snPtr;
      j=1;
      w=sequenceLength-1;
      while(w--)
	*snPtr+=snPtr[j++];
      ps = snPtr+1;
      w=segTable[i]-sequenceLength; // +1 - 1
      while(w--){
	tmp2=*ps;
	*ps=*(ps-1)-tmp1+*(ps+sequenceLength);
	tmp1=tmp2;
	ps++;
      }
    }
    snPtr+=segTable[i];
  }
  
  double* pn = sMeanL2;
  w=dbH->numFiles;
  while(w--)
    *pn++=0.0;
  ps=sNorm;
  unsigned processedSegs=0;
  for(i=0; i<dbH->numFiles; i++){
    if(segTable[i]>sequenceLength-1){
      w = segTable[i]-sequenceLength+1;
      pn = sMeanL2+i;
      while(w--)
	*pn+=*ps++;
      *pn/=segTable[i]-sequenceLength+1;
      SILENCE_THRESH+=*pn;
      processedSegs++;
    }
    ps = sNorm + segTable[i];
  }
  if(verbosity>1)
    cerr << "processedSegs: " << processedSegs << endl;
  SILENCE_THRESH/=processedSegs;
  USE_THRESH=1; // Turn thresholding on
  DIFF_THRESH=SILENCE_THRESH/=2; // 50% of the mean shingle power
  SILENCE_THRESH/=10; // 10% of the mean shingle power is SILENCE
  
  w=sequenceLength-1;
  i=1;
  tmp1=*qNorm;
  while(w--)
    *qNorm+=qNorm[i++];
  ps = qNorm+1;
  qMeanL2 = *qNorm;
  w=numVectors-sequenceLength;
  while(w--){
    tmp2=*ps;
    *ps=*(ps-1)-tmp1+*(ps+sequenceLength);
    tmp1=tmp2;
    qMeanL2+=*ps;
    *ps++;
  }
  qMeanL2 /= numVectors-sequenceLength+1;
  if(verbosity>1)
    cerr << "done." << endl;    
  
  
  if(verbosity>1)
    cerr << "matching segs..." << endl;
  
  assert(pointNN>0 && pointNN<=O2_MAXNN);
  assert(segNN>0 && segNN<=O2_MAXNN);
  
  // Make temporary dynamic memory for results
  double segDistances[segNN];
  unsigned segIDs[segNN];
  unsigned segQIndexes[segNN];
  unsigned segSIndexes[segNN];
  
  double distances[pointNN];
  unsigned qIndexes[pointNN];
  unsigned sIndexes[pointNN];
  

  unsigned k,l,m,n,seg,segOffset=0, HOP_SIZE=sequenceHop, wL=sequenceLength;
  double thisDist;
  double oneOverWL=1.0/wL;
  
  for(k=0; k<pointNN; k++){
    distances[k]=0.0;
    qIndexes[k]=~0;
    sIndexes[k]=~0;    
  }
  
  for(k=0; k<segNN; k++){
    segDistances[k]=0.0;
    segQIndexes[k]=~0;
    segSIndexes[k]=~0;
    segIDs[k]=~0;
  }

  // Timestamp and durations processing
  double meanQdur = 0;
  double* timesdata = 0;
  double* meanDBdur = 0;
  
  if(usingTimes && !(dbH->flags & O2_FLAG_TIMES)){
    cerr << "warning: ignoring query timestamps for non-timestamped database" << endl;
    usingTimes=0;
  }
  
  else if(!usingTimes && (dbH->flags & O2_FLAG_TIMES))
    cerr << "warning: no timestamps given for query. Ignoring database timestamps." << endl;
  
  else if(usingTimes && (dbH->flags & O2_FLAG_TIMES)){
    timesdata = new double[numVectors];
    assert(timesdata);
    insertTimeStamps(numVectors, timesFile, timesdata);
    // Calculate durations of points
    for(k=0; k<numVectors-1; k++){
      timesdata[k]=timesdata[k+1]-timesdata[k];
      meanQdur+=timesdata[k];
    }
    meanQdur/=k;
    if(verbosity>1)
      cerr << "mean query file duration: " << meanQdur << endl;
    meanDBdur = new double[dbH->numFiles];
    assert(meanDBdur);
    for(k=0; k<dbH->numFiles; k++){
      meanDBdur[k]=0.0;
      for(j=0; j<segTable[k]-1 ; j++)
	meanDBdur[k]+=timesTable[j+1]-timesTable[j];
      meanDBdur[k]/=j;
    }
  }

  if(usingQueryPoint)
    if(queryPoint>numVectors || queryPoint>numVectors-wL+1)
      error("queryPoint > numVectors-wL+1 in query");
    else{
      if(verbosity>1)
	cerr << "query point: " << queryPoint << endl; cerr.flush();
      query=query+queryPoint*dbH->dim;
      qNorm=qNorm+queryPoint;
      numVectors=wL;
    }
  
  double ** D = 0;    // Cross-correlation between query and target 
  double ** DD = 0;   // Matched filter distance

  D = new double*[numVectors];
  assert(D);
  DD = new double*[numVectors];
  assert(DD);

  gettimeofday(&tv1, NULL); 
  processedSegs=0;
  unsigned successfulSegs=0;

  double* qp;
  double* sp;
  double* dp;
  double diffL2;

  // build segment offset table
  unsigned *segOffsetTable = new unsigned[dbH->numFiles];
  unsigned cumSeg=0;
  unsigned segIndexOffset;
  for(k=0; k<dbH->numFiles;k++){
    segOffsetTable[k]=cumSeg;
    cumSeg+=segTable[k]*dbH->dim;
  }

  char nextKey [MAXSTR];
  for(processedSegs=0, seg=0 ; processedSegs < dbH->numFiles ; seg++, processedSegs++){

    // get segID from file if using a control file
    if(segFile){
      if(!segFile->eof()){
	segFile->getline(nextKey,MAXSTR);
	seg=getKeyPos(nextKey);
      }
      else
	break;
    }

    segOffset=segOffsetTable[seg];     // numDoubles offset
    segIndexOffset=segOffset/dbH->dim; // numVectors offset

    if(sequenceLength<segTable[seg]){  // test for short sequences
      
      if(verbosity>7)
	cerr << seg << "." << segIndexOffset << "." << segTable[seg] << " | ";cerr.flush();
		
      // Cross-correlation matrix
      for(j=0; j<numVectors;j++){
	D[j]=new double[segTable[seg]]; 
	assert(D[j]);

      }

      // Matched filter matrix
      for(j=0; j<numVectors;j++){
	DD[j]=new double[segTable[seg]];
	assert(DD[j]);
      }

      // Cross Correlation
      for(j=0; j<numVectors; j++)
	for(k=0; k<segTable[seg]; k++){
	  qp=query+j*dbH->dim;
	  sp=dataBuf+segOffset+k*dbH->dim;
	  DD[j][k]=0.0; // Initialize matched filter array
	  dp=&D[j][k];  // point to correlation cell j,k
	  *dp=0.0;      // initialize correlation cell
	  l=dbH->dim;         // size of vectors
	  while(l--)
	    *dp+=*qp++**sp++;
	}
  
      // Matched Filter
      // HOP SIZE == 1
      double* spd;
      if(HOP_SIZE==1){ // HOP_SIZE = shingleHop
	for(w=0; w<wL; w++)
	  for(j=0; j<numVectors-w; j++){ 
	    sp=DD[j];
	    spd=D[j+w]+w;
	    k=segTable[seg]-w;
	    while(k--)
	      *sp+++=*spd++;
	  }
      }
      else{ // HOP_SIZE != 1
	for(w=0; w<wL; w++)
	  for(j=0; j<numVectors-w; j+=HOP_SIZE){
	    sp=DD[j];
	    spd=D[j+w]+w;
	    for(k=0; k<segTable[seg]-w; k+=HOP_SIZE){
	      *sp+=*spd;
	      sp+=HOP_SIZE;
	      spd+=HOP_SIZE;
	    }
	  }
      }
      
      if(verbosity>3 && usingTimes){
	cerr << "meanQdur=" << meanQdur << " meanDBdur=" << meanDBdur[seg] << endl;
	cerr.flush();
      }

      if(!usingTimes || 
	 (usingTimes 
	  && fabs(meanDBdur[seg]-meanQdur)<meanQdur*timesTol)){

	if(verbosity>3 && usingTimes){
	  cerr << "within duration tolerance." << endl;
	  cerr.flush();
	}

	// Search for minimum distance by shingles (concatenated vectors)
	for(j=0;j<numVectors-wL+1;j+=HOP_SIZE)
	  for(k=0;k<segTable[seg]-wL+1;k+=HOP_SIZE){
	    
	    diffL2 = fabs(qNorm[j] - sNorm[k]);
	    // Power test
	    if(!USE_THRESH || 
	       // Threshold on mean L2 of Q and S sequences
	       (USE_THRESH && qNorm[j]>SILENCE_THRESH && sNorm[k]>SILENCE_THRESH && 
		// Are both query and target windows above mean energy?
		(qNorm[j]>qMeanL2 && sNorm[k]>sMeanL2[seg] &&  diffL2 < DIFF_THRESH )))
	      thisDist=DD[j][k]*oneOverWL;
	    else
	      thisDist=0.0;
	    
	    // NBest match algorithm
	    for(m=0; m<pointNN; m++){
	      if(thisDist>=distances[m]){	  
		// Shuffle distances up the list
		for(l=pointNN-1; l>m; l--){
		  distances[l]=distances[l-1];
		  qIndexes[l]=qIndexes[l-1];
		  sIndexes[l]=sIndexes[l-1];
		}
		distances[m]=thisDist;
		if(usingQueryPoint)
		  qIndexes[m]=queryPoint;
		else
		  qIndexes[m]=j;
		sIndexes[m]=k;
		break;
	      }
	    }
	  }
	// Calculate the mean of the N-Best matches
	thisDist=0.0;
	for(m=0; m<pointNN; m++)
	  thisDist+=distances[m];
	thisDist/=pointNN;
	
	// Let's see the distances then...
	if(verbosity>3)
	  cerr << "d[" << fileTable+seg*O2_FILETABLESIZE << "]=" << thisDist << endl;

	// All the seg stuff goes here
	n=segNN;
	while(n--){
	  if(thisDist>=segDistances[n]){
	    if((n==0 || thisDist<=segDistances[n-1])){
	      // Copy all values above up the queue
	      for( l=segNN-1 ; l > n ; l--){
		segDistances[l]=segDistances[l-1];
		segQIndexes[l]=segQIndexes[l-1];
		segSIndexes[l]=segSIndexes[l-1];
		segIDs[l]=segIDs[l-1];
	      }
	      segDistances[n]=thisDist;
	      segQIndexes[n]=qIndexes[0];
	      segSIndexes[n]=sIndexes[0];
	      successfulSegs++;
	      segIDs[n]=seg;
	      break;
	    }
	  }
	  else
	    break;
	}
      } // Duration match
      
      // per-seg reset array values
      for(unsigned k=0; k<pointNN; k++){
	distances[k]=0.0;
	qIndexes[k]=~0;
	sIndexes[k]=~0;    
      }
      
      // Clean up current seg
      if(D!=NULL){
	for(j=0; j<numVectors; j++)
	  delete[] D[j];
      }

      if(DD!=NULL){
	for(j=0; j<numVectors; j++)
	  delete[] DD[j];
      }
    }
  }

  gettimeofday(&tv2,NULL);
  if(verbosity>1)
    cerr << endl << "processed segs :" << processedSegs << " matched segments: " << successfulSegs << " elapsed time:" 
	 << ( tv2.tv_sec*1000 + tv2.tv_usec/1000 ) - ( tv1.tv_sec*1000+tv1.tv_usec/1000 ) << " msec" << endl;
  
  if(adbQueryResult==0){
    if(verbosity>1)
      cerr<<endl;
    // Output answer
    // Loop over nearest neighbours
    for(k=0; k < min(segNN,successfulSegs); k++)
      cout << fileTable+segIDs[k]*O2_FILETABLESIZE << " " << segDistances[k] << " " << segQIndexes[k] << " " << segSIndexes[k] << endl;
  }
  else{ // Process Web Services Query
    int listLen = min(segNN, processedSegs);
    adbQueryResult->__sizeRlist=listLen;
    adbQueryResult->__sizeDist=listLen;
    adbQueryResult->__sizeQpos=listLen;
    adbQueryResult->__sizeSpos=listLen;
    adbQueryResult->Rlist= new char*[listLen];
    adbQueryResult->Dist = new double[listLen];
    adbQueryResult->Qpos = new int[listLen];
    adbQueryResult->Spos = new int[listLen];
    for(k=0; k<adbQueryResult->__sizeRlist; k++){
      adbQueryResult->Rlist[k]=new char[O2_MAXFILESTR];
      adbQueryResult->Dist[k]=segDistances[k];
      adbQueryResult->Qpos[k]=segQIndexes[k];
      adbQueryResult->Spos[k]=segSIndexes[k];
      sprintf(adbQueryResult->Rlist[k], "%s", fileTable+segIDs[k]*O2_FILETABLESIZE);
    }
  }


  // Clean up
  if(segOffsetTable)
    delete segOffsetTable;
  if(queryCopy)
    delete queryCopy;
  //if(qNorm)
  //delete qNorm;
  if(D)
    delete[] D;
  if(DD)
    delete[] DD;
  if(timesdata)
    delete timesdata;
  if(meanDBdur)
    delete meanDBdur;


}

void audioDB::normalize(double* X, int dim, int n){
  unsigned c = n*dim;
  double minval,maxval,v,*p;

  p=X;  
  while(c--){
    v=*p++;
    if(v<minval)
      minval=v;
    else if(v>maxval)
      maxval=v;
  }

  normalize(X, dim, n, minval, maxval);

}

void audioDB::normalize(double* X, int dim, int n, double minval, double maxval){
  unsigned c = n*dim;
  double *p;


  if(maxval==minval)
    return;

  maxval=1.0/(maxval-minval);
  c=n*dim;
  p=X;

  while(c--){
    *p=(*p-minval)*maxval;
    p++;
  }
}

// Unit norm block of features
void audioDB::unitNorm(double* X, unsigned dim, unsigned n, double* qNorm){
  unsigned d;
  double L2, oneOverL2, *p;
  if(verbosity>2)
    cerr << "norming " << n << " vectors...";cerr.flush();
  while(n--){
    p=X;
    L2=0.0;
    d=dim;
    while(d--){
      L2+=*p**p;
      p++;
    }
    L2=sqrt(L2);
    if(qNorm)
      *qNorm++=L2;
    oneOverL2 = 1.0/L2;
    d=dim;
    while(d--){
      *X*=oneOverL2;
      X++;
    }
  }
  if(verbosity>2)
    cerr << "done..." << endl;
}

// Unit norm block of features
void audioDB::unitNormAndInsertL2(double* X, unsigned dim, unsigned n, unsigned append=0){
  unsigned d;
  double L2, oneOverL2, *p;
  unsigned nn = n;

  assert(l2normTable);

  if( !append && (dbH->flags & O2_FLAG_L2NORM) )
    error("Database is already L2 normed", "automatic norm on insert is enabled");

  if(verbosity>2)
    cerr << "norming " << n << " vectors...";cerr.flush();

  double* l2buf = new double[n];
  double* l2ptr = l2buf;
  assert(l2buf);
  assert(X);

  while(nn--){
    p=X;
    *l2ptr=0.0;
    d=dim;
    while(d--){
      *l2ptr+=*p**p;
      p++;
    }
    *l2ptr=sqrt(*l2ptr);
    oneOverL2 = 1.0/(*l2ptr++);
    d=dim;
    while(d--){
      *X*=oneOverL2;
      X++;
    }
  }
  unsigned offset;
  if(append)
    offset=dbH->length/(dbH->dim*sizeof(double)); // number of vectors
  else
    offset=0;
  memcpy(l2normTable+offset, l2buf, n*sizeof(double));
  if(l2buf)
    delete l2buf;
  if(verbosity>2)
    cerr << "done..." << endl;
}


// Start an audioDB server on the host
void audioDB::startServer(){
  struct soap soap;
  int m, s; // master and slave sockets
  soap_init(&soap);
  m = soap_bind(&soap, NULL, port, 100);
  if (m < 0)
    soap_print_fault(&soap, stderr);
  else
    {
      fprintf(stderr, "Socket connection successful: master socket = %d\n", m);
      for (int i = 1; ; i++)
	{
	  s = soap_accept(&soap);
	  if (s < 0)
	    {
	      soap_print_fault(&soap, stderr);
	      break;
	    }
	  fprintf(stderr, "%d: accepted connection from IP=%d.%d.%d.%d socket=%d\n", i,
		  (soap.ip >> 24)&0xFF, (soap.ip >> 16)&0xFF, (soap.ip >> 8)&0xFF, soap.ip&0xFF, s);
	  if (soap_serve(&soap) != SOAP_OK) // process RPC request
	    soap_print_fault(&soap, stderr); // print error
	  fprintf(stderr, "request served\n");
	  soap_destroy(&soap); // clean up class instances
	  soap_end(&soap); // clean up everything and close socket
	}
    }
  soap_done(&soap); // close master socket and detach environment
} 


// web services

// SERVER SIDE
int adb__status(struct soap* soap, xsd__string dbName, xsd__int &adbCreateResult){
  char* const argv[]={"audioDB",COM_STATUS,dbName};
  const unsigned argc = 3;
  audioDB(argc,argv);
  adbCreateResult=100;
  return SOAP_OK;
}

// Literal translation of command line to web service

int adb__query(struct soap* soap, xsd__string dbName, xsd__string qKey, xsd__string keyList, xsd__string timesFileName, xsd__int qType, xsd__int qPos, xsd__int pointNN, xsd__int segNN, xsd__int seqLen, adb__queryResult &adbQueryResult){
  char queryType[256];
  for(int k=0; k<256; k++)
    queryType[k]='\0';
  if(qType == O2_FLAG_POINT_QUERY)
    strncpy(queryType, "point", strlen("point"));
  else if (qType == O2_FLAG_SEQUENCE_QUERY)
    strncpy(queryType, "sequence", strlen("sequence"));
  else if(qType == O2_FLAG_SEG_QUERY)
    strncpy(queryType,"segment", strlen("segment"));
  else
    strncpy(queryType, "", strlen(""));

  if(pointNN==0)
    pointNN=10;
  if(segNN==0)
    segNN=10;
  if(seqLen==0)
    seqLen=16;

  char qPosStr[256];
  sprintf(qPosStr, "%d", qPos);
  char pointNNStr[256];
  sprintf(pointNNStr,"%d",pointNN);
  char segNNStr[256];
  sprintf(segNNStr,"%d",segNN);
  char seqLenStr[256];  
  sprintf(seqLenStr,"%d",seqLen);
  
  const  char* argv[] ={
    "./audioDB", 
    COM_QUERY, 
    queryType, // Need to pass a parameter
    COM_DATABASE,
    dbName, 
    COM_FEATURES,
    qKey, 
    COM_KEYLIST,
    keyList==0?"":keyList,
    COM_TIMES,
    timesFileName==0?"":timesFileName,
    COM_QPOINT, 
    qPosStr,
    COM_POINTNN,
    pointNNStr,
    COM_SEGNN,
    segNNStr, // Need to pass a parameter
    COM_SEQLEN,
    seqLenStr
  };

  const unsigned argc = 19;
  audioDB(argc, (char* const*)argv, &adbQueryResult);
  return SOAP_OK;
}

int main(const unsigned argc, char* const argv[]){
  audioDB(argc, argv);
}


