#include <chrono>
#include <math.h>
#include <sys/stat.h>

#include "H5Cpp.h"
using namespace H5;

long GetFileSize(std::string filename)
{
  struct stat stat_buf;
  int rc = stat(filename.c_str(), &stat_buf);
  return rc == 0 ? stat_buf.st_size : -1;
}

void loop_write(const char* filename, int loop_limit, hsize_t chunk_size, hsize_t num_bytes){
    const H5std_string FILE_NAME(filename);
    const H5std_string DATASETNAME("ExtendibleArray");

    std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

    hsize_t dims[2] = {1,num_bytes};        // dataset dimensions at creation
    hsize_t maxdims[2] = {H5S_UNLIMITED, num_bytes}; 
    hsize_t chunk_dims[2] ={chunk_size, num_bytes};
    int32_t  data[1][num_bytes];//= { {1, 1}};    // data to write 

    // Variables used in extending and writing to the extended portion of dataset 
    
    hsize_t size[2];
    hsize_t offset[2];
    hsize_t dimsext[2] = {1, num_bytes};         // extend dimensions 
    //    int loop_limit = 10;

    // Create a new file using the default property lists. 
    H5File file(FILE_NAME, H5F_ACC_TRUNC);

    DataSpace *dataspace = new DataSpace (2, dims, maxdims);

    // Modify dataset creation property to enable chunking
    DSetCreatPropList prop;
    prop.setChunk(2, chunk_dims);

    // Create the chunked dataset.  Note the use of pointer.
    DataSet *dataset = new DataSet(file.createDataSet( DATASETNAME, 
                                                       PredType::STD_I32LE, *dataspace, prop) );
    //int i=0;
    // Write data to dataset.
    // dataset->write(data, PredType::NATIVE_INT);

    for(int i=0; i<loop_limit; i++){
        // Extend the dataset. Dataset becomes n+1 x 3.
        size[0] = dims[0] + i*dimsext[0];
        size[1] = dims[1];
        dataset->extend(size); 

        // Make some random numbers
        for(hsize_t j=0; i<dimsext[1]; i++){
        data[0][i] = rand();
        }
        // Select a hyperslab in extended portion of the dataset.
        DataSpace *filespace = new DataSpace(dataset->getSpace ());
        offset[0] = size[0]-1;
        offset[1] = 0;
        filespace->selectHyperslab(H5S_SELECT_SET, dimsext, offset);

        // Define memory space.
        DataSpace *memspace = new DataSpace(2, dimsext, NULL);
        // Write data to the extended portion of the dataset.
        dataset->write(data, PredType::NATIVE_INT, *memspace, *filespace);
        // delete filespace;
        delete filespace;
        delete memspace;

         };

    // Close all objects and file.
    prop.close();
    delete dataspace;
    delete dataset;
    file.close();

    std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();

    int duration = std::chrono::duration_cast<std::chrono::milliseconds>( t2 - t1 ).count();
    auto fileSize = (float) GetFileSize(FILE_NAME)/1000000; //MB
    float av_speed = 1000*fileSize/(duration); // MB/s
    float av_freq = float(loop_limit)/duration;
    float hdf_ratio = 1000000*fileSize/(num_bytes*loop_limit*4.0);


    printf("%-20i%-20i%-20i%-20i%-20.2f%-20.2f%-20.2f%-20.2f\n", chunk_size , loop_limit, num_bytes, duration, fileSize, hdf_ratio, av_speed, av_freq); 
        };

int main (int argc, char *argv[])
{

    const H5std_string FILE_NAME(argv[1]);   
    const H5std_string DATASETNAME("ExtendibleArray");

    printf("%-20s%-20s%-20s%-20s%-20s%-20s%-20s%-20s\n", "Chunk size", "Loop limit", "Bytes/extension", "Duration (ms)", "Filesize (MB)", "HDF Ratio", "Write speed (MB/s)", "Frequency (kHz)"); 

    int loop_limit = 1000000;
    int num_bytes = 2;
    for(size_t  i=0; pow(2,i)<loop_limit; i++){
    // loop limit, chunk size, integers per extension
        loop_write(argv[1], loop_limit,pow(2,i),num_bytes);
    };
    return 0;  // successfully terminated
}
