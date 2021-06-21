#pragma once

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>

#include "debug.h"
#include "../utils/hashutil.h"
#include "packedtable.h"
#include "printutil.h"
#include "singletable.h"
#include "better_table.h"

namespace cuckoofilter {
  uint64_t ROUNDDOWN(uint64_t a, uint32_t b) {
    return ((a) - ((a) % (b)));
  }
  
  uint64_t ROUNDUP(uint64_t a, uint32_t b) {
    return ROUNDDOWN((a) + (b - 1), b);
  }

//const int big_seg = 262144;
  const int AR = 4;
  const int BATCH_SIZE = 128;
  
  int max(int x, int y) {
    if (x > y) return x;
    return y;
  }
  
  double balls_in_bins_max_load(double balls, double bins) {
    double m = balls;
    double n = bins;
    if (n == 1) return m;
    double ret = (m / n) + 1.5 * sqrt(2 * m / n * log(n));
    return ret;
  }
  
  int proper_alt_range(int M, int i, int *len) {
    double b = 4;      // slots per bucket
    double lf = 0.95;  // target load factor
    int alt_range = 8;
    for (; alt_range < M;) {
      double f = (4 - i) * 0.25;
      if (balls_in_bins_max_load(f * b * lf * M, M * 1.0 / alt_range) <
          0.97 * b * alt_range) {
        return alt_range;
      }
      alt_range <<= 1;
    }
    return alt_range;
  }
  
  enum Status {
    Ok = 0,
    NotFound = 1,
    NotEnoughSpace = 2,
    NotSupported = 3,
  };

// maximum number of cuckoo kicks before claiming failure
  const size_t kMaxCuckooCount = 500;

// A cuckoo filter class exposes a Bloomier filter interface,
// providing methods of Add, Delete, Contain. It takes three
// template parameters:
//   ItemType:  the type of item you want to insert
//   bits_per_item: how many bits each item is hashed into
//   TableType: the storage of table, SingleTable by default, and
// PackedTable to enable semi-sorting
  template<typename ItemType, size_t bits_per_item, template<size_t> class TableType = BetterTable, typename HashFamily = TwoIndependentMultiplyShift>
  class VacuumFilter {
    // Storage of items
    TableType<bits_per_item> *table_;
    
    // Number of items stored
    size_t num_items_;
    
    typedef struct {
      size_t index;
      uint32_t tag;
      bool used;
    } VictimCache;
    
    VictimCache victim_;
    
    HashFamily hasher_;
    
    int big_seg;
    int len[AR];
    
    inline size_t IndexHash(uint32_t hv) const {
      return (((uint64_t) hv * (uint64_t) table_->NumBuckets()) >> 32U);
    }
    
    inline uint32_t TagHash(uint32_t hv) const {
      uint32_t tag;
      tag = hv & ((1ULL << bits_per_item) - 1);
      tag += (tag == 0);
      return tag;
    }
    
    inline void GenerateIndexTagHash(const ItemType &item, size_t *index, uint32_t *tag) const {
      const uint64_t hash = hasher_(item);
      *index = IndexHash(hash >> 32);
      *tag = TagHash(hash);
    }
    
    inline size_t AltIndex(int index, const uint32_t tag) const {
      // NOTE(binfan): originally we use:
      // index ^ HashUtil::BobHash((const void*) (&tag), 4)) & table_->INDEXMASK;
      // now doing a quick-n-dirty way:
      // 0x5bd1e995 is the hash constant from MurmurHash2
      //return IndexHash((uint32_t)(index ^ (tag * 0x5bd1e995)));
      int t = tag * 0x5bd1e995;
      int seg = len[tag & (AR - 1)];
      t = t & seg;
      t += (t == 0);
      return index ^ t;
    }
    
    Status AddImpl(const size_t i, const uint32_t tag);
  
  public:
    explicit VacuumFilter(const size_t max_num_keys, bool aligned = false) :
        num_items_(0), victim_(), hasher_() {
      srand(1);
      size_t assoc = 4;
      size_t num_buckets;
      
      if (aligned) {
        num_buckets = upperpower2(std::max<uint64_t>(1, max_num_keys / 0.96 / assoc));
        if (num_buckets < 128) num_buckets = 128;
        
        for (int i = 0; i < AR; i++)
          len[i] = num_buckets - 1;
      } else {
        num_buckets = (max_num_keys / 0.96 / 4);
        big_seg = 1024;
        big_seg = max(big_seg, proper_alt_range(num_buckets, 0, len));
        num_buckets = ROUNDUP(num_buckets, big_seg);
        
        big_seg--;
        len[0] = big_seg;
        for (int i = 1; i < AR; i++)
          len[i] = proper_alt_range(num_buckets, i, len) - 1;
        len[AR - 1] = (len[AR - 1] + 1) * 2 - 1;
        if (AR == 1) num_buckets = ROUNDUP(num_buckets, len[0] + 1);
      }
      
      victim_.used = false;
      
      //std::cout << "num_buckets = " << num_buckets << std::endl;
      //std::cout << "big_segment_length = " << big_seg << std::endl;
      std::cout << "alt range setting : ";
      for (int i = 0; i < AR; i++)
        std::cout << len[i] + 1 << ", ";
      std::cout << std::endl;
      
      table_ = new TableType<bits_per_item>(num_buckets);
    }
    
    ~VacuumFilter() { delete table_; }
    
    // Add an item to the filter.
    Status Add(const ItemType &item);
    
    // Add an item to the filter.
    void Add_many(const ItemType *item, bool *result, int key_n);
    
    bool evict(const size_t i, const uint32_t tag);
    
    // Report if the item is inserted, with false positive rate.
    Status Contain(const ItemType &item) const;
    
    // Report if the item is inserted, with false positive rate.
    void Contain_many(const ItemType *item, bool *result, int key_n);
    
    // Report if the item is inserted, report bucket 1 or bucket 2
    uint32_t Contain_where(const ItemType &item) const;
    
    // Delete an key from the filter
    Status Delete(const ItemType &item);
    
    // Delete an key from the filter
    void Delete_many(const ItemType *item, bool *result, int key_n);
    
    /* methods for providing stats  */
    // summary infomation
    std::string Info() const;
    
    // number of current inserted items;
    size_t Size() const { return num_items_; }
    
    // size of the filter in bytes.
    size_t SizeInBytes() const { return table_->SizeInBytes(); }
    
    // load factor is the fraction of occupancy
    double LoadFactor() const { return 1.0 * Size() / table_->SizeInTags(); }
    
    double BitsPerItem() const { return 8.0 * table_->SizeInBytes() / Size(); }
  };
  
  template<typename ItemType, size_t bits_per_item,
      template<size_t> class TableType, typename HashFamily>
  Status VacuumFilter<ItemType, bits_per_item, TableType, HashFamily>::Add(const ItemType &item) {
    size_t i;
    uint32_t tag;
    
    GenerateIndexTagHash(item, &i, &tag);
    return AddImpl(i, tag);
  }
  
  template<typename ItemType, size_t bits_per_item,
      template<size_t> class TableType, typename HashFamily>
  void VacuumFilter<ItemType, bits_per_item, TableType, HashFamily>::Add_many(
      const ItemType *key, bool *result, int key_n) {
    size_t bucket_position[BATCH_SIZE];
    size_t alt_position[BATCH_SIZE];
    uint32_t tag[BATCH_SIZE];
    bool tmp_result[BATCH_SIZE];
    uint32_t oldtag;
    uint32_t tags[4];
    
    for (int i = 0; i < key_n; i += BATCH_SIZE) {
      int up = std::min(BATCH_SIZE, key_n - i);
      memset(tmp_result, false, BATCH_SIZE);
      for (int j = 0; j < up; j++)
        GenerateIndexTagHash(key[i + j], &bucket_position[j], &tag[j]);
      for (int j = 0; j < up; j++)
        tmp_result[j] = table_->InsertTagToBucket(bucket_position[j], tag[j], false, oldtag, tags);
      for (int j = 0; j < up; j++)
        alt_position[j] = AltIndex(bucket_position[j], tag[j]);
      for (int j = 0; j < up; j++)
        if (tmp_result[j] == false)
          tmp_result[j] = table_->InsertTagToBucket(alt_position[j], tag[j], false, oldtag, tags);
      for (int j = 0; j < up; j++)
        if (tmp_result[j] == false)
          tmp_result[j] = evict(bucket_position[j], tag[j]);
      for (int j = 0; j < up; j++)
        num_items_ += tmp_result[j];
      memcpy(&result[i], tmp_result, BATCH_SIZE);
    }
  }
  
  template<typename ItemType, size_t bits_per_item,
      template<size_t> class TableType, typename HashFamily>
  bool VacuumFilter<ItemType, bits_per_item, TableType, HashFamily>::evict(const size_t i, const uint32_t tag) {
    size_t curindex = i;
    uint32_t curtag = tag;
    uint32_t oldtag;
    
    uint32_t tags[4];
    uint32_t tmp_tags[4];
    
    //table_->ReadBucket(curindex, tags);
    
    for (uint32_t count = 0; count < kMaxCuckooCount; count++) {
      //bool kickout = count > 0;
      oldtag = 0;
      if (table_->InsertTagToBucket(curindex, curtag, false, oldtag, tags)) {
        return true;
      }
      for (int i = 0; i < 4; i++) {
        int alt = AltIndex(curindex, tags[i]);
        if (table_->InsertTagToBucket(alt, tags[i], false, oldtag, tmp_tags)) {
          tags[i] = curtag;
          
          table_->WriteTag(curindex, i, curtag);
          return true;
        }
      }
      
      int r = rand() % 4;
      oldtag = tags[r];
      tags[r] = curtag;
      table_->WriteTag(curindex, r, curtag);
      curtag = oldtag;
      curindex = AltIndex(curindex, curtag);
    }
    
    return false;
  }
  
  template<typename ItemType, size_t bits_per_item,
      template<size_t> class TableType, typename HashFamily>
  Status VacuumFilter<ItemType, bits_per_item, TableType, HashFamily>::AddImpl(const size_t i, const uint32_t tag) {
    size_t curindex = i;
    uint32_t curtag = tag;
    uint32_t oldtag;
    
    uint32_t tags[4];
    uint32_t tmp_tags[4];
    
    if (table_->InsertTagToBucket(curindex, curtag, false, oldtag, tags)) {
      num_items_++;
      return Ok;
    }
    
    uint32_t altbucket = AltIndex(curindex, curtag);
    if (table_->InsertTagToBucket(altbucket, curtag, false, oldtag, tags)) {
      num_items_++;
      return Ok;
    }
    
    for (uint32_t count = 0; count < kMaxCuckooCount; count++) {
      oldtag = 0;
      if (table_->InsertTagToBucket(curindex, curtag, false, oldtag, tags)) {
        num_items_++;
        return Ok;
      }
      for (int i = 0; i < 4; i++) {
        int alt = AltIndex(curindex, tags[i]);
        if (table_->InsertTagToBucket(alt, tags[i], false, oldtag, tmp_tags)) {
          tags[i] = curtag;
          table_->WriteTag(curindex, i, curtag);
          
          num_items_++;
          return Ok;
        }
      }
      
      int r = rand() % 4;
      oldtag = tags[r];
      tags[r] = curtag;
      table_->WriteTag(curindex, r, curtag);
      curtag = oldtag;
      curindex = AltIndex(curindex, curtag);
    }
    
    if (victim_.used) {
      return NotEnoughSpace;
    }
    
    victim_.index = curindex;
    victim_.tag = curtag;
    victim_.used = true;
    return Ok;
  }
  
  template<typename ItemType, size_t bits_per_item,
      template<size_t> class TableType, typename HashFamily>
  Status VacuumFilter<ItemType, bits_per_item, TableType, HashFamily>::Contain(const ItemType &key) const {
    size_t i1, i2;
    uint32_t tag;
    
    GenerateIndexTagHash(key, &i1, &tag);
    
    if ((victim_.used && i1 == victim_.index && tag == victim_.tag) || table_->FindTagInBucket(i1, tag)) return Ok;
    
    i2 = AltIndex(i1, tag);
    
    if ((victim_.used && i2 == victim_.index && tag == victim_.tag) || table_->FindTagInBucket(i2, tag)) return Ok;
    
    return NotFound;
  }
  
  template<typename ItemType, size_t bits_per_item,
      template<size_t> class TableType, typename HashFamily>
  void VacuumFilter<ItemType, bits_per_item, TableType, HashFamily>::Contain_many(
      const ItemType *key, bool *result, int key_n) {
    size_t bucket_position[BATCH_SIZE];
    uint32_t tag[BATCH_SIZE];
    bool tmp_result[BATCH_SIZE];
    
    for (int i = 0; i < key_n; i += BATCH_SIZE) {
      memset(tmp_result, false, BATCH_SIZE);
      for (int j = 0; j < BATCH_SIZE; j++)
        GenerateIndexTagHash(key[i + j], &bucket_position[j], &tag[j]);
      for (int j = 0; j < BATCH_SIZE; j++)
        tmp_result[j] = table_->FindTagInBucket(bucket_position[j], tag[j]);
      for (int j = 0; j < BATCH_SIZE; j++)
        bucket_position[j] = AltIndex(bucket_position[j], tag[j]);
      for (int j = 0; j < BATCH_SIZE; j++)
        if (tmp_result[j] == false)
          tmp_result[j] = table_->FindTagInBucket(bucket_position[j], tag[j]);
      
      memcpy(&result[i], tmp_result, BATCH_SIZE);
    }
  }
  
  template<typename ItemType, size_t bits_per_item,
      template<size_t> class TableType, typename HashFamily>
  uint32_t VacuumFilter<ItemType, bits_per_item, TableType, HashFamily>::Contain_where(
      const ItemType &key) const {
    size_t i1, i2;
    uint32_t tag;
    
    GenerateIndexTagHash(key, &i1, &tag);
    
    if ((victim_.used && i1 == victim_.index && tag == victim_.tag) || table_->FindTagInBucket(i1, tag)) return 1;
    
    i2 = AltIndex(i1, tag);
    
    if ((victim_.used && i2 == victim_.index && tag == victim_.tag) || table_->FindTagInBucket(i2, tag)) return 2;
    
    return 0;
  }
  
  template<typename ItemType, size_t bits_per_item,
      template<size_t> class TableType, typename HashFamily>
  void VacuumFilter<ItemType, bits_per_item, TableType, HashFamily>::Delete_many(
      const ItemType *key, bool *result, int key_n) {
    size_t bucket_position[BATCH_SIZE];
    uint32_t tag[BATCH_SIZE];
    bool tmp_result[BATCH_SIZE];
    
    for (int i = 0; i < key_n; i += BATCH_SIZE) {
      int up = std::min(BATCH_SIZE, key_n - i);
      memset(tmp_result, false, BATCH_SIZE);
      for (int j = 0; j < up; j++)
        GenerateIndexTagHash(key[i + j], &bucket_position[j], &tag[j]);
      for (int j = 0; j < up; j++)
        tmp_result[j] = table_->DeleteTagFromBucket(bucket_position[j], tag[j]);
      for (int j = 0; j < up; j++)
        bucket_position[j] = AltIndex(bucket_position[j], tag[j]);
      for (int j = 0; j < up; j++)
        if (tmp_result[j] == false)
          tmp_result[j] = table_->DeleteTagFromBucket(bucket_position[j], tag[j]);
      for (int j = 0; j < up; j++)
        num_items_ -= tmp_result[j];
      memcpy(&result[i], tmp_result, BATCH_SIZE);
    }
  }
  
  template<typename ItemType, size_t bits_per_item,
      template<size_t> class TableType, typename HashFamily>
  Status VacuumFilter<ItemType, bits_per_item, TableType, HashFamily>::Delete(const ItemType &key) {
    size_t i1, i2;
    uint32_t tag;
    
    GenerateIndexTagHash(key, &i1, &tag);
    
    if (table_->DeleteTagFromBucket(i1, tag)) {
      num_items_--;
      goto TryEliminateVictim;
    }
    i2 = AltIndex(i1, tag);
    if (table_->DeleteTagFromBucket(i2, tag)) {
      num_items_--;
      goto TryEliminateVictim;
    } else if (victim_.used && tag == victim_.tag &&
               (i1 == victim_.index || i2 == victim_.index)) {
      // num_items_--;
      victim_.used = false;
      return Ok;
    } else {
      return NotFound;
    }
    TryEliminateVictim:
    if (victim_.used) {
      victim_.used = false;
      size_t i = victim_.index;
      uint32_t tag = victim_.tag;
      AddImpl(i, tag);
    }
    return Ok;
  }
  
  template<typename ItemType, size_t bits_per_item,
      template<size_t> class TableType, typename HashFamily>
  std::string VacuumFilter<ItemType, bits_per_item, TableType, HashFamily>::Info() const {
    std::stringstream ss;
    ss << "CuckooFilter Status:\n"
       << "\t\t" << table_->Info() << "\n"
       << "\t\tKeys stored: " << Size() << "\n"
       << "\t\tLoad factor: " << LoadFactor() << "\n"
       << "\t\tHashtable size: " << (table_->SizeInBytes() >> 10) << " KB\n";
    if (Size() > 0) {
      ss << "\t\tbit/key:   " << BitsPerItem() << "\n";
    } else {
      ss << "\t\tbit/key:   N/A\n";
    }
    return ss.str();
  }
}  // namespace cuckoofilter
