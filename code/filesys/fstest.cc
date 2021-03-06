// fstest.cc 
//    Simple test routines for the file system.  
//
//    We implement:
//       Copy -- copy a file from UNIX to Nachos
//       Print -- cat the contents of a Nachos file 
//       Perftest -- a stress test for the Nachos file system
//        read and write a really large file in tiny chunks
//        (won't work on baseline system!)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "utility.h"
#include "filesys.h"
#include "directory.h"
#include "system.h"
#include "thread.h"
#include "disk.h"
#include "stats.h"

#include <time.h> 

#define TransferSize     10     // make it small, just to be difficult

//----------------------------------------------------------------------
// Copy
//     Copy the contents of the UNIX file "from" to the Nachos file "to"
//----------------------------------------------------------------------

void
Copy(const char *from, const char *to)
{
    FILE *fp;
    OpenFile* openFile;
    char *buffer;
    int amountRead, fileLength;

// Open UNIX file
    if ((fp = fopen(from, "r")) == NULL) {     
        printf("Copy: couldn't open input file %s\n", from);
        return;
    }
    
    fseek(fp, 0, 2);		
    fileLength = ftell(fp);
    fseek(fp, 0, 0);
    
    OpenFile* ofile = fileSystem->Open(from);
    if (ofile){
        ofile->header()->permission(FileHeader::Write);
        ofile->SaveHeader();
        
        fileSystem->Close(ofile);
        fileSystem->Remove(from);
    }

// Create a Nachos file of the same length
    printf("Copying file %s, to file %s\n", from, to);
    switch (fileSystem->Create(to, fileLength, FileHeader::File, FileHeader::Write)) {     // Create Nachos file
    case E_PERM:        
        printf("Copy: couldn't create output file %s: permission denied.\n", to);
        fclose(fp);
        return;
        break;
    case E_BLOCK:        
        printf("Copy: couldn't create output file %s: no blocks free remaining.\n", to);
        fclose(fp);
        return;
        break;
    case E_DIRECTORY:        
        printf("Copy: couldn't create output file %s: directory is full.\n", to);
        fclose(fp);
        return;
        break;
    case E_DISK:        
        printf("Copy: couldn't create output file %s: disk is full.\n", to);
        fclose(fp);
        return;
        break;
    case E_EXIST:        
        printf("Copy: couldn't create output file %s: file already exists.\n", to);
        fclose(fp);
        return;
        break;
    case E_SUCESS:        
        printf("Copy: created output file %s.\n", to);
        break;
    default:    
        printf("Copy: couldn't create output file %s: unknown error.\n", to);
        fclose(fp);
        return;
        break;
    }
    
    openFile = fileSystem->Open(to);
    ASSERT(openFile != NULL);
    
// Copy the data in TransferSize chunks
    buffer = new char[TransferSize];
    while ((amountRead = fread(buffer, sizeof(char), TransferSize, fp)) > 0){
	int amountWrite;
	if ((amountWrite = openFile->Write(buffer, amountRead)) != amountRead){
	    if (amountWrite < 0){
		printf("Copy: Permission denied.\n");
		break;
	    }
	    else
		printf("Copy: Out of memory.\n");
	}
    }
    delete [] buffer;

// Close the UNIX and the Nachos files
    
    openFile->header()->clearPermission(FileHeader::Write);
    openFile->SaveHeader();
    fileSystem->Close(openFile);
    fclose(fp);
}

/*!
 * Create a new directory to the given path
 * \param *path full path
 */

void
MakeDirectory(const char *path)
{
// Create a Nachos file of the same length
    printf("Creating directory %s\n", path);
    switch (fileSystem->Create(path, 0, FileHeader::Directory)) {     // Create Nachos file
        case E_BLOCK:        
            printf("Copy: couldn't create output directory %s: no blocks free remaining.\n", path);
            return;
            break;
        case E_DIRECTORY:        
            printf("Copy: couldn't create output directory %s: directory is full.\n", path);
            break;
        case E_DISK:        
            printf("Copy: couldn't create output directory %s: disk is full.\n", path);
            return;
            break;
        case E_EXIST:        
            printf("Copy: couldn't create output directory %s: file already exists.\n", path);
            break;
        case E_SUCESS:        
            printf("Copy: created output directory %s.\n", path);
            break;
        default:    
            printf("Copy: couldn't create output directory %s: unknown error.\n", path);
            return;
            break;
    }
    
    OpenFile* openFile = fileSystem->Open(path);
    ASSERT(openFile != NULL);
    fileSystem->Close(openFile);
}

//----------------------------------------------------------------------
// Print
//     Print the contents of the Nachos file "name".
//----------------------------------------------------------------------

void
Print(const char *name)
{
    OpenFile *openFile;    
    int i, amountRead;
    char *buffer;

    if ((openFile = fileSystem->Open(name)) == NULL) {
    printf("Print: unable to open file %s\n", name);
    return;
    }
    
    buffer = new char[openFile->Length()];
    
    while ((amountRead = openFile->Read(buffer, openFile->Length())) > 0){
    for (i = 0; i < amountRead; i++){
        printf("%c", buffer[i]);
    }
    }
        
    printf("\n");
    
    delete [] buffer;
    
    fileSystem->Close(openFile);    // close the Nachos file
    return;
}

static void _show_tree_worker(OpenFile* file, int level){
    ASSERT(file->type() == (int)FileHeader::Directory);
    
    Directory* curr = new Directory;
    curr->FetchFrom(file);
    
    char* space = new char[level + 1];
    
    for (int i = 0; i < level; i++)
        space[i] = '\t';
    space[level] = '\0';
    
    char perm[] = "d---";
	
    //~ if (file->header()->permission() & FileHeader::Read) perm[1] = 'r';
    //~ if (file->header()->permission() & FileHeader::Write) perm[2] = 'w';
    //~ if (file->header()->permission() & FileHeader::Exec) perm[3] = 'x';
    
    time_t epoch_time_as_time_t = 1514764800 + file->header()->lastaccess();
    struct tm *timeinfo = localtime(&epoch_time_as_time_t);
    char timestamp [16];
    //~ strftime (timestamp,16,"%D %R",timeinfo);
    
    //~ printf("%s\t%s\t%d\t.\n", perm, timestamp, file->Length());
    //~ if (curr->parent()){   
	//~ if (file->header()->permission() & FileHeader::Read) perm[1] = 'r';
	//~ if (file->header()->permission() & FileHeader::Write) perm[2] = 'w';
	//~ if (file->header()->permission() & FileHeader::Exec) perm[3] = 'x';
	
	//~ epoch_time_as_time_t = 1514764800 + curr->parent()->header()->lastaccess();
	//~ timeinfo = localtime(&epoch_time_as_time_t);
	//~ strftime (timestamp,16,"%D %R",timeinfo);
	
	//~ printf("%s\t%s\t%d\t..\n", perm, timestamp, curr->parent()->Length());
    //~ }
    
    for (int i = 2; i < curr->count(); i++){
        OpenFile* curr_file = curr->get_item(i);    
	
	if (!curr_file){
	    printf("\t\t\t<unreadable>");
	    continue;
	}
    
	epoch_time_as_time_t = 1514764800 + curr_file->header()->lastaccess();
	timeinfo = localtime(&epoch_time_as_time_t);
	strftime (timestamp,16,"%D %R",timeinfo);
	
	perm[0] = curr_file->type() == FileHeader::Directory ? 'd' : '-';
	perm[1] = curr_file->header()->permission() & FileHeader::Read ? 'r' : '-';
	perm[2] = curr_file->header()->permission() & FileHeader::Write ? 'w' : '-';
	perm[3] = curr_file->header()->permission() & FileHeader::Exec ? 'x': '-';    
	
        printf("%s\t%s\t%d\t%s%s\n", perm, timestamp, curr_file->Length(), space, curr->get_name(i));
        if (curr_file->type() == FileHeader::Directory)
            _show_tree_worker(curr_file, level+1);
        delete curr_file;
    }
    delete curr;
    delete [] space;
}
void 
ShowTree()
{
    OpenFile *openFile = fileSystem->Open("/");
    
    ASSERT(openFile && openFile->type() == FileHeader::Directory);
    
    printf("Free space: %d sectors on %d\n", fileSystem->freeSector(), NumSectors);
    printf("\n");
    _show_tree_worker(openFile, 0);
    printf("\n");
}

//----------------------------------------------------------------------
// PerformanceTest
//     Stress the Nachos file system by creating a large file, writing
//    it out a bit at a time, reading it back a bit at a time, and then
//    deleting the file.
//
//    Implemented as three separate routines:
//      FileWrite -- write the file
//      FileRead -- read the file
//      PerformanceTest -- overall control, and print out performance #'s
//----------------------------------------------------------------------

#define FileName     "TestFile"
#define DirName     "TestDir"
#define Contents     "1234567890"
#define ContentSize     strlen(Contents)
#define FileSize     ((int)(ContentSize * 5000))

void 
QuickTest()
{ 
    int errorCode;
    if ((errorCode = fileSystem->Create("/test_file_1", 10, FileHeader::File, FileHeader::Read | FileHeader::Write))) {
      printf("Perf test: can't create /test_file_1: %d\n", errorCode);
      return;
    }
    if ((errorCode = fileSystem->Move("/test_file_1", "/" DirName "/" DirName "/"))) {
      printf("Perf test: can't move file: %d\n", errorCode);
      return;
    }
    if (!(errorCode = fileSystem->Remove("/" DirName "/" DirName "/test_file_1"))) {
      printf("Perf test: can't Remove /%s/%s/test_file_1: %d\n", DirName, DirName, errorCode);
      return;
    }
    if ((errorCode = fileSystem->Create("/test_file_1", 0, FileHeader::File, FileHeader::Read | FileHeader::Write))) {
      printf("Perf test: can't create /test_file_1 again: %d\n", errorCode);
      return;
    }
    OpenFile* file = fileSystem->Open("/test_file_1");
    if (!file) {
      printf("Perf test: can't Open /test_file_1: %d\n", errorCode);
      return;
    }
    if (!(errorCode = fileSystem->Move("/test_file_1", "/" DirName ))) {
      printf("Perf test: can move file, should not\n");
      return;
    }
    if ((errorCode = fileSystem->Remove("/test_file_1"))) {
      printf("Perf test: can remove file, should not\n");
      return;
    }
    char* data = new char[1024];
    for (int i = 0; i < 1024; i++)
    data[i] = 'a' + (char)(i % 26);
    file->WriteAt(data, 1024);
    
    printf("Trying to display in an other object\n");
    Print("/test_file_1");
    
    for (int i = 0; i < 120; i++)
    file->Write(data, 1024);
    
    fileSystem->Close(file);
    
    if ((errorCode = fileSystem->Create("/my_super_long_file_name", 0, FileHeader::File, FileHeader::Read | FileHeader::Write))) {
      printf("Perf test: can't create /my_super_long_file_name: %d\n", errorCode);
      return;
    }
    OpenFile* file2 = fileSystem->Open("/my_super_long_file_name");
    
    for (int i = 0; i < 80; i++)
    file2->Write(data, 128);
    
    fileSystem->Close(file2);
    if ((errorCode = fileSystem->Move("/my_super_long_file_name", "/" DirName "/" DirName "/"))) {
      printf("Perf test: can't move file: %d\n", errorCode);
      return;
    }
    
    if (!(errorCode = fileSystem->Remove("/test_file_1"))) {
      printf("Perf test: cannot remove the file\n");
      return;
    }
    if ((errorCode = fileSystem->Create("/test_file_1", 0, FileHeader::File, FileHeader::Read | FileHeader::Write))) {
      printf("Perf test: can't create /test_file_1 again: %d\n", errorCode);
      return;
    }
    file = fileSystem->Open("/test_file_1");
    if (!file) {
      printf("Perf test: can't open /test_file_1 again: %d\n", errorCode);
      return;
    }
    
    for (int i = 0; i < 1024; i++)
    data[i] = 'a' + (char)(i % 26);
    file->WriteAt(data, 1024);
    delete [] data;
    
    fileSystem->Close(file);
    
    if (!(errorCode = fileSystem->Remove("/" DirName "/" DirName "/my_super_long_file_name"))) {
      printf("Perf test: cannot remove the file\n");
      return;
    }
}

void 
ChangeMod(char* file, const char* perm){
    int flag;
    if (!atoi(perm) && (strlen(perm) > 1 || (char)atoi(perm) + '0' != perm[0]))
	flag = ((strrchr(perm, 'x')!=0) << 2) | ((strrchr(perm, 'w')!=0) << 1) | (strrchr(perm, 'r')!=0); 
    else
	flag = atoi(perm);
    printf("Octal permission: %d...\n", flag);
    
    OpenFile* ofile = fileSystem->Open(file);
    if (!ofile){
	printf("Can't open the file\n");
	return;
    }
    ofile->header()->permission(flag);
    ofile->SaveHeader();
    
    fileSystem->Close(ofile);
}

void 
MakeTree()
{ 
    int errorCode;
    printf("Tree structure simulation...\n");
    if ((errorCode = fileSystem->Create("/" DirName, 0, FileHeader::Directory, FileHeader::Read | FileHeader::Write))) {
      printf("Perf test: can't create /%s/: %d\n", DirName, errorCode);
      return;
    }
    if ((errorCode = fileSystem->Create("/" DirName "/" DirName, 0, FileHeader::Directory, FileHeader::Read | FileHeader::Write))) {
      printf("Perf test: can't create /%s/%s/: %d\n", DirName, DirName, errorCode);
      return;
    }
    if ((errorCode = fileSystem->Create(FileName, 0, FileHeader::File, FileHeader::Read | FileHeader::Write))) {
      printf("Perf test: can't create /%s: %d\n", FileName, errorCode);
      return;
    }
    if ((errorCode = fileSystem->Create("/" DirName "/" FileName, 0, FileHeader::File, FileHeader::Read | FileHeader::Write))) {
      printf("Perf test: can't create /%s/%s: %d\n", DirName, FileName, errorCode);
      return;
    }
    if ((errorCode = fileSystem->Create("/" DirName "/" DirName "/" FileName, 0, FileHeader::File, FileHeader::Read | FileHeader::Write))) {
      printf("Perf test: can't create /%s/%s/%s: %d\n", DirName, DirName, FileName, errorCode);
      return;
    }    // close file
}

static void 
FileWrite()
{
    OpenFile *openFile;    
    int i, numBytes;

    printf("Sequential write of %d byte file, in %zd byte chunks\n", 
    FileSize, ContentSize);
    if (fileSystem->Create(FileName, 0)) {
      printf("Perf test: can't create %s\n", FileName);
      return;
    }
    openFile = fileSystem->Open(FileName);
    if (openFile == NULL) {
    printf("Perf test: unable to open %s\n", FileName);
    return;
    }
    for (i = 0; i < FileSize; i += ContentSize) {
        numBytes = openFile->Write(Contents, ContentSize);
    if (numBytes < 10) {
        printf("Perf test: unable to write %s\n", FileName);
        delete openFile;
        return;
    }
    }
    delete openFile;    // close file
}

static void 
FileRead()
{
    OpenFile *openFile;    
    char *buffer = new char[ContentSize];
    int i, numBytes;

    printf("Sequential read of %d byte file, in %zd byte chunks\n", 
    FileSize, ContentSize);

    if ((openFile = fileSystem->Open(FileName)) == NULL) {
    printf("Perf test: unable to open file %s\n", FileName);
    delete [] buffer;
    return;
    }
    for (i = 0; i < FileSize; i += ContentSize) {
        numBytes = openFile->Read(buffer, ContentSize);
    if ((numBytes < 10) || strncmp(buffer, Contents, ContentSize)) {
        printf("Perf test: unable to read %s\n", FileName);
        delete openFile;
        delete [] buffer;
        return;
    }
    }
    delete [] buffer;
    delete openFile;    // close file
}

void
PerformanceTest()
{
    printf("Starting file system performance test:\n");
    stats->Print();
    FileWrite();
    FileRead();
    if (!fileSystem->Remove(FileName)) {
      printf("Perf test: unable to remove %s\n", FileName);
      return;
    }
    stats->Print();
}

