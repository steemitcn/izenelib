#ifndef IZENELIB_IR_RANDOMREADTEST_H_
#define IZENELIB_IR_RANDOMREADTEST_H_
#include <ir/index_manager/index/rtype/BTreeIndexer.h>
#include <ir/index_manager/index/rtype/InMemoryBTreeIndexer.h>

#define TEST_DEBUG 

using namespace izenelib::ir::indexmanager;

template <class KeyType>
class RandomReadTest
{
typedef CBTreeIndexer<KeyType> IndexerType;
typedef uint32_t docid_t;
typedef std::vector<docid_t> RefValueType;
typedef izenelib::ir::indexmanager::Compare<KeyType> CompareType;
typedef InMemoryBTreeIndexer<KeyType, docid_t> RefType;

public:

    static bool Test(IndexerType& indexer, RefType& ref)
    {
        KeyType key;
        RandomGenerator<KeyType>::Gen(key);
        uint32_t func_count = 7;
        uint32_t func_num = 0;
        RandomGenerator<uint32_t>::Gen(0, func_count-1, func_num);
        bool result = false;
        BitVector docs1;
        BitVector docs2;
        switch (func_num) 
        {
            case 0:
#ifdef TEST_DEBUG
                std::cout<<"seek "<<key<<std::endl;
#endif
                result = indexer.seek(key) == ref.seek(key);
                break;
            case 1:
#ifdef TEST_DEBUG
                std::cout<<"getValue "<<key<<std::endl;
#endif
                indexer.getValue(key, docs1);
                ref.getValue(key, docs2);
                result = docs1.equal_ignore_size(docs2);
                break;
            case 2:
#ifdef TEST_DEBUG
                std::cout<<"getValueLess "<<key<<std::endl;
#endif
                indexer.getValueLess(key, docs1);
                ref.getValueLess(key, docs2);
                result = docs1.equal_ignore_size(docs2);
                break;
            case 3:
#ifdef TEST_DEBUG
                std::cout<<"getValueLessEqual "<<key<<std::endl;
#endif
                indexer.getValueLessEqual(key, docs1);
                ref.getValueLessEqual(key, docs2);
                result = docs1.equal_ignore_size(docs2);
                break;
            case 4:
#ifdef TEST_DEBUG
                std::cout<<"getValueGreat "<<key<<std::endl;
#endif
                indexer.getValueGreat(key, docs1);
                ref.getValueGreat(key, docs2);
                result = docs1.equal_ignore_size(docs2);
// #ifdef TEST_DEBUG
//                 while(!result)
//                 {
//                     std::cout<<"retring getValueGreat...."<<std::endl;
//                     docs1.clear();
//                     docs2.clear();
//                     indexer.getValueGreat(key-1, docs1);
//                     ref.getValueGreat(key-1, docs2);
//                     result = docs1.equal_ignore_size(docs2);
//                 }
// #endif
                break;
            case 5:
#ifdef TEST_DEBUG
                std::cout<<"getValueGreatEqual "<<key<<std::endl;
#endif
                indexer.getValueGreatEqual(key, docs1);
                ref.getValueGreatEqual(key, docs2);
                result = docs1.equal_ignore_size(docs2);
// #ifdef TEST_DEBUG
//                 while(!result)
//                 {
//                     std::cout<<"retring getValueGreatEqual...."<<std::endl;
//                     docs1.clear();
//                     docs2.clear();
//                     indexer.getValueGreatEqual(key-1, docs1);
//                     ref.getValueGreatEqual(key-1, docs2);
//                     result = docs1.equal_ignore_size(docs2);
//                 }
// #endif
                break;
            case 6:
            {
                KeyType key2;
                RandomGenerator<KeyType>::Gen(key2);
#ifdef TEST_DEBUG
                std::cout<<"getValueBetween "<<key<<","<<key2<<std::endl;
#endif
                indexer.getValueBetween(key, key2, docs1);
                ref.getValueBetween(key, key2, docs2);
                result = docs1.equal_ignore_size(docs2);
            }
                break;
            default:
                result = false;
        }
#ifdef TEST_DEBUG
        if(!result)
        {
            std::cout<<"failed reason:"<<std::endl;
            std::cout<<docs1<<std::endl;
            std::cout<<docs2<<std::endl;
        }
#endif
        std::cout<<"[docs count]"<<docs1.count()<<","<<docs2.count()<<std::endl;

        return result;
    }
    
};


template <>
class RandomReadTest<izenelib::util::UString>
{
typedef izenelib::util::UString KeyType;
typedef CBTreeIndexer<KeyType> IndexerType;
typedef uint32_t docid_t;
typedef std::vector<docid_t> RefValueType;
typedef izenelib::ir::indexmanager::Compare<KeyType> CompareType;
typedef InMemoryBTreeIndexer<KeyType, docid_t> RefType;

public:

    static bool Test(IndexerType& indexer, RefType& ref)
    {
        KeyType key;
        RandomGenerator<KeyType>::Gen(key);
        uint32_t func_count = 10;
        uint32_t func_num = 0;
        RandomGenerator<uint32_t>::Gen(0, func_count-1, func_num);
        bool result = false;
        BitVector docs1;
        BitVector docs2;
        switch (func_num) 
        {
            case 0:
                result = indexer.seek(key) == ref.seek(key);
                break;
            case 1:
                indexer.getValue(key, docs1);
                ref.getValue(key, docs2);
                result = docs1.equal_ignore_size(docs2);
                break;
            case 2:
                indexer.getValueLess(key, docs1);
                ref.getValueLess(key, docs2);
                result = docs1.equal_ignore_size(docs2);
                break;
            case 3:
                indexer.getValueLessEqual(key, docs1);
                ref.getValueLessEqual(key, docs2);
                result = docs1.equal_ignore_size(docs2);
                break;
            case 4:
                indexer.getValueGreat(key, docs1);
                ref.getValueGreat(key, docs2);
                result = docs1.equal_ignore_size(docs2);
                break;
            case 5:
                indexer.getValueGreatEqual(key, docs1);
                ref.getValueGreatEqual(key, docs2);
                result = docs1.equal_ignore_size(docs2);
                break;
            case 6:
            {
                KeyType key2;
                RandomGenerator<KeyType>::Gen(key2);
                indexer.getValueBetween(key, key2, docs1);
                ref.getValueBetween(key, key2, docs2);
                result = docs1.equal_ignore_size(docs2);
            }
                break;
            case 7:
                indexer.getValueStart(key, docs1);
                ref.getValueStart(key, docs2);
                result = docs1.equal_ignore_size(docs2);
                break;
            case 8:
                indexer.getValueEnd(key, docs1);
                ref.getValueEnd(key, docs2);
                result = docs1.equal_ignore_size(docs2);
                break;
            case 9:
                indexer.getValueSubString(key, docs1);
                ref.getValueSubString(key, docs2);
                result = docs1.equal_ignore_size(docs2);
                break;
            default:
                result = false;
        }
        return result;
    }
    
};

#endif


