#include "common.h"
#include "Smash/client.h"
#include "ceph.h"
#include <boost/asio/ip/host_name.hpp>
#include <experimental/filesystem>

class YCSB {
public:
  int recordcount = 1000;
  int operationcount = 1000;
  double readproportion = 0.5;
  double updateproportion = 0.5;
  double readmodifywriteproportion = 0;
  Distribution requestdistribution = zipfian;
  int parallel = 1;
  int id;
  
  vector<char> buffer;  // for sending value
  vector<K> keys;
  
  void loadKeys(uint32_t size) {
    if (!filesystem::is_regular_file("dist/keys.txt")) {
      cerr << "Key file not present. " << endl;
      exit(-1);
    }
    
    cout << "loading keys" << endl;
    string key;  // all commands:
    ifstream f("dist/keys.txt");
    int i = 0;
    while (!f.eof() && keys.size() < size) {
      getline(f, key);
      i++;
      if (i <= keys.size()) continue;
      keys.push_back(key);
    }
    cout << "loaded" << endl;
  }
  
  template<class C>
  inline void load() {
    C client;
    loadKeys(recordcount);
    
    Clocker c("load");
    for (int i = id % parallel; i < recordcount; i += parallel) {
      sprintf((char *) buffer.data(), "%s#%d...", keys[i].c_str(), i);
      client.Insert(keys[i], buffer.data());
    }
  }
  
  template<class C>
  inline void run() {
    C client;
    loadKeys(recordcount);
    Clocker c("run");
    
    for (int i = id % parallel; i < operationcount; i += parallel) {
      double r = (double) rand() / RAND_MAX;
      string &k = keys[InputBase::rand()];
      
      if (r < readproportion + readmodifywriteproportion) { // read
        buffer = client.Read(k);
      }
      
      if (r > readproportion) {  // update. may read and update
        sprintf((char *) buffer.data(), "%s#%d...", k.c_str(), i);
        client.Update(k, buffer.data());
      }
    }
  }
  
  inline void loadSmash() {
    load<Client>();
  }
  
  inline void runSmash() {
    run<Client>();
  }
  
  inline void loadCeph() {
    load<CephClient>();
  }
  
  inline void runCeph() {
    run<CephClient>();
  }
  
  YCSB(const string workload, int parallel = 1, int override_records = 1000) : parallel(parallel) {
    string host = boost::asio::ip::host_name();
    string ids = host.substr(string("node-").length(), host.find('.'));
    id = atoi(ids.c_str());
    cout << "host name: " << host << ", id: " << id << endl;
    
    buffer.resize(blockSize);
    
    if (workload[0] == 'a') {
      recordcount = override_records;
      operationcount = 1000;
      readproportion = 0.5;
      updateproportion = 0.5;
//      scanproportion = 0;
//      insertproportion = 0;
      requestdistribution = zipfian;
      
    } else if (workload[0] == 'b') {
      recordcount = override_records;
      operationcount = 1000;
      readproportion = 0.95;
      updateproportion = 0.05;
//      scanproportion = 0;
//      insertproportion = 0;
      requestdistribution = zipfian;
      
    } else if (workload[0] == 'c') {
      recordcount = override_records;
      operationcount = 1000;
      readproportion = 1;
      updateproportion = 0;
//      scanproportion = 0;
//      insertproportion = 0;
      requestdistribution = zipfian;
//    } else if (workload[0] == 'd') {
//      recordcount = 1000;
//      operationcount = 1000;
//      readproportion = 0.95;
//      updateproportion = 0;
//      scanproportion = 0;
//      insertproportion = 0.05;
//      requestdistribution = latest;
//
//    } else if (workload[0] == 'e') {
//      recordcount = 1000;
//      operationcount = 1000;
//      readproportion = 0;
//      updateproportion = 0;
//      scanproportion = 0.95;
//      insertproportion = 0.05;
//      requestdistribution = zipfian;
//      maxscanlength = 100;
//      scanlengthdistribution = uniform;
    } else if (workload[0] == 'f') {
      recordcount = override_records;
      operationcount = 1000;
      readproportion = 0.5;
      updateproportion = 0;
//      scanproportion = 0;
//      insertproportion = 0;
      readmodifywriteproportion = 0.5;
      requestdistribution = zipfian;
    } else {
      ifstream f(workload);
      
      f >> recordcount;
      f >> operationcount;
      f >> readproportion;
      f >> updateproportion;
//      f >> scanproportion;
//      f >> insertproportion;
      f >> readmodifywriteproportion;
      
      string s;
      f >> s;
      if (s == "zipfian") {
        requestdistribution = zipfian;
      } else {
        requestdistribution = uniform;
      }
      
      f.close();
    }
    
    InputBase::setSeed(1);
    InputBase::distribution = requestdistribution;
    InputBase::bound = recordcount;
  }
};
