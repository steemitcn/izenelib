#ifndef IZENELIB_UTIL_BLOOM_FILTER_H
#define IZENELIB_UTIL_BLOOM_FILTER_H

#include <cmath>
#include <limits.h>
#include <stdexcept>
#include <types.h>

#include <util/hashFunction.h>

namespace izenelib { namespace util {

/**
 * Standard BloomFilter
 */
template <typename KeyType, typename HashType = uint32_t>
class BloomFilter
{
    enum{CHARBIT = 8};
public:
    BloomFilter()
        :num_bits_(0)
        ,num_bytes_(0)
        ,bloom_bits_(0)
    {}

    BloomFilter(size_t items_estimate, float false_positive_prob)
    {
        items_estimate_ = items_estimate;
        false_positive_prob_ = false_positive_prob;
        double num_hashes = -std::log(false_positive_prob_) / std::log(2);
        num_hash_functions_ = (size_t)num_hashes;
        num_bits_ = (size_t)(items_estimate_ * num_hashes / std::log(2));
        if (num_bits_ == 0)
        {
            throw std::runtime_error("Empty bloom filter");
        }
        num_bytes_ = (num_bits_ / CHARBIT) + (num_bits_ % CHARBIT ? 1 : 0);
        bloom_bits_ = new uint8_t[num_bytes_];

        memset(bloom_bits_, 0, num_bytes_);

        std::cout <<"num funcs="<< num_hash_functions_
                     <<" num bits="<< num_bits_ <<" num bytes="<< num_bytes_
                     <<" bits per element="<< double(num_bits_) / items_estimate
                     << std::endl;
    }

    BloomFilter(size_t items_estimate, float bits_per_item, size_t num_hashes)
    {
        items_estimate_ = items_estimate;
        false_positive_prob_ = 0.0;
        num_hash_functions_ = num_hashes;
        num_bits_ = (size_t)((double)items_estimate * (double)bits_per_item);
        if (num_bits_ == 0)
        {
            throw std::runtime_error("Empty bloom filter");
        }
        num_bytes_ = (num_bits_ / CHARBIT) + (num_bits_ % CHARBIT ? 1 : 0);
        bloom_bits_ = new uint8_t[num_bytes_];

        memset(bloom_bits_, 0, num_bytes_);

        std::cout <<"num funcs="<< num_hash_functions_
                     <<" num bits="<< num_bits_ <<" num bytes="<< num_bytes_
                     <<" bits per element="<< double(num_bits_) / items_estimate
                     << std::endl;
    }

    BloomFilter(size_t items_estimate, int64_t length, size_t num_hashes)
    {
        items_estimate_ = items_estimate;
        false_positive_prob_ = 0.0;
        num_hash_functions_ = num_hashes;
        num_bits_ = (size_t)length;
        if (num_bits_ == 0)
        {
            throw std::runtime_error("Empty bloom filter");
        }
        num_bytes_ = (num_bits_ / CHARBIT) + (num_bits_ % CHARBIT ? 1 : 0);
        bloom_bits_ = new uint8_t[num_bytes_];

        memset(bloom_bits_, 0, num_bytes_);

        std::cout <<"num funcs="<< num_hash_functions_
                     <<" num bits="<< num_bits_ <<" num bytes="<< num_bytes_
                     <<" bits per element="<< double(num_bits_) / items_estimate
                     << std::endl;
    }

    ~BloomFilter()
    {
        if(bloom_bits_) delete[] bloom_bits_;
    }

    void Insert(const KeyType& key)
    {
        for (size_t i = 0; i < num_hash_functions_; ++i)
        {
            uint32_t hash = hasher_(key) % num_bits_;
            bloom_bits_[hash / CHARBIT] |= (1 << (hash % CHARBIT));
        }
    }

    bool Get(const KeyType& key) const
    {
        uint8_t byte_mask;
        uint8_t byte;
        for (size_t i = 0; i < num_hash_functions_; ++i)
        {
            uint32_t hash = hasher_(key) % num_bits_;
            byte = bloom_bits_[hash / CHARBIT];
            byte_mask = (1 << (hash % CHARBIT));

            if ( (byte & byte_mask) == 0 )
            {
                return false;
            }
        }
        return true;
    }

    template<class DataIO> friend
    void DataIO_loadObject(DataIO& dio, BloomFilter& x)
    {
        dio & x.items_estimate_;
        dio & x.false_positive_prob_;
        dio & x.num_hash_functions_;
        dio & x.num_bits_;
        dio & x.num_bytes_;
        x.bloom_bits_ = new uint8_t[x.num_bytes_];
        dio.ensureRead(x.bloom_bits_, sizeof(uint8_t)*x.num_bytes_);
    }
 
    template<class DataIO> friend
    void DataIO_saveObject(DataIO& dio, const BloomFilter& x)
    {
        dio & x.items_estimate_;
        dio & x.false_positive_prob_;
        dio & x.num_hash_functions_;
        dio & x.num_bits_;
        dio & x.num_bytes_;
        dio.ensureWrite(x.bloom_bits_, sizeof(uint8_t)*x.num_bytes_);
    }

    size_t Size()
    {
        return num_bytes_;
    }
private:
    DISALLOW_COPY_AND_ASSIGN(BloomFilter);	

    HashIDTraits<KeyType,HashType> hasher_;
    size_t items_estimate_;
    float false_positive_prob_;
    size_t num_hash_functions_;
    size_t num_bits_;
    size_t num_bytes_;
    uint8_t *bloom_bits_;
};

}}

#endif // IZENELIB_UTIL_BLOOM_FILTER_H
