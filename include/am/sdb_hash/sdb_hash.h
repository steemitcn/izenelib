#ifndef SDB_HASH_H
#define SDB_HASH_H

#include <string>
#include <map>
#include <iostream>
#include <types.h>
#include <sys/stat.h>

//#include <util/log.h>
#include <stdio.h>

#include "sh_types.h"
#include "sdb_hash_header.h"
#include "string_chain.h"

using namespace std;

NS_IZENELIB_AM_BEGIN

template< typename KeyType, typename ValueType, typename LockType =NullLock> class sdb_hash :
public AccessMethod<KeyType, ValueType, LockType>
{
public:
	typedef std::pair<string_chain*, char*> NodeKeyLocn;
	typedef DataType<KeyType,ValueType> DataType;
public:
	sdb_hash(const string& fileName = "sdb_hash.dat"):fileName_(fileName) {			
	}

	virtual ~sdb_hash() {
		close();
		dataFile_ = 0;
	}

	void setBucketSize(size_t bucketSize) {
		sfh_.bucketSize = bucketSize;
	}

	void setDirectorySize(size_t dirSize) {
		sfh_.directorySize = dirSize;

	}

	void setCacheSize(size_t cacheSize)
	{
		sfh_.cacheSize = cacheSize;
		if(sfh_.cacheSize < sfh_.directorySize)
			sfh_.cacheSize = sfh_.directorySize;
	}

	bool insert(const DataType& dat) {
		return insert(dat.get_key(), dat.get_value() );
	}

	bool insert(const KeyType& key, const ValueType& value) {
		
		NodeKeyLocn locn;
		if( search(key, locn) )
		return false;
		else {
			DbObjPtr ptr, ptr1;

			ptr.reset(new DbObj);
			ptr1.reset(new DbObj);
			write_image(key, ptr);
			write_image(value, ptr1);

			size_t ksize = ptr->getSize();
			size_t vsize = ptr1->getSize();

			string_chain* sa = locn.first;
			char* p = locn.second;

			//entry_[idx] is even NULL.
			if(locn.first == NULL) {
				uint32_t idx = sdb_hashing::hash_fun(ptr->getData(), ptr->getSize() )
				% sfh_.directorySize;

				if (bucketAddr[idx] == 0) {
					entry_[idx] = allocateBlock_();
					bucketAddr[idx] = entry_[idx]->fpos;

					sa = entry_[idx];
					p = entry_[idx]->str;
				}
			}
			else
			{			
				//cout<<sa->level<<endl;
				//sa->display();
				assert(locn.second != NULL);
				size_t gap = sizeof(long)+sizeof(int)+ksize+vsize+2*sizeof(size_t);				

				//add an extra size_t to indicate if reach the end of  string_chain.
				if ( size_t(p - sa->str) > sfh_.bucketSize-gap-sizeof(size_t) ) {
					if (sa->next == 0) {
						sa->isDirty = true;
						sa->next = allocateBlock_();						
					}
					sa = sa->loadNext(dataFile_);
					p = sa->str;
				}
			}
			
			memcpy(p, &ksize, sizeof(size_t));
			p += sizeof(size_t);
			memcpy(p, &vsize, sizeof(size_t));
			p += sizeof(size_t);
			memcpy(p, ptr->getData(), ksize);
			p += ksize;
			memcpy(p, ptr1->getData(), vsize);
			p += vsize;

			assert( size_t (p-sa->str) + sizeof(long) +sizeof(int) <= sfh_.bucketSize);
			
			sa->isDirty = true;
			sa->num++;
			sfh_.numItems++;
			return true;
		}
	
	}

	ValueType* find(const KeyType & key) {

		NodeKeyLocn locn;
		if( !search(key, locn) )
		return NULL;
		else {
			char *p = locn.second;
			size_t ksz, vsz;
			memcpy(&ksz, p, sizeof(size_t));
			p += sizeof(size_t);
			memcpy(&vsz, p, sizeof(size_t));
			p += sizeof(size_t);

			ValueType* pv;
			ValueType val;

			DbObjPtr ptr;
			ptr.reset( new DbObj(p+ksz, vsz) );
			read_image(val, ptr);
			pv = new ValueType(val);

			return pv;
		}		
	}

	bool del(const KeyType& key) {

		NodeKeyLocn locn;
		if( !search(key, locn) )
		return false;
		else
		{
			char *p = locn.second;
			size_t ksz, vsz;
			memcpy(&ksz, p, sizeof(size_t));
			p += sizeof(size_t);
			memcpy(&vsz, p, sizeof(size_t));
			p += sizeof(size_t);

			memset(p, 0, ksz);

			/* 
			 //this is much slower.
			 vsz += ksz;
			 ksz = 0;
			 memcpy(p-2*sizeof(size_t), &ksz, sizeof(size_t) );
			 memcpy(p-sizeof(size_t), &vsz, sizeof(size_t) );*/

			locn.first->isDirty = true;
			--sfh_.numItems;
			return true;
		}

	}

	bool update(const DataType& dat)
	{
		return update( dat.get_key(), dat.get_value() );
	}

	bool update(const KeyType& key, const ValueType& value) {
		NodeKeyLocn locn;
		if( !search(key, locn) )
		return insert(key, value);
		else
		{
			DbObjPtr ptr, ptr1;
			ptr.reset(new DbObj);
			ptr1.reset(new DbObj);
			write_image(key, ptr);
			write_image(value, ptr1);

			size_t ksize = ptr->getSize();
			size_t vsize = ptr1->getSize();

			string_chain* sa = locn.first;
			char *p = locn.second;
			size_t ksz, vsz;
			memcpy(&ksz, p, sizeof(size_t));
			p += sizeof(size_t);
			memcpy(&vsz, p, sizeof(size_t));
			p += sizeof(size_t);
			if(vsz == vsize)
			{
				sa->isDirty = true;
				memcpy(p+ksz, ptr1->getData(), vsz);
				return true;
			}
			else
			{
				sa->isDirty = true;
				memset(p, 0, ksize);
				return insert(key, value);
			}
			return true;
		}
		
	}

	bool search(const KeyType&key, NodeKeyLocn& locn)
	{
		flushCache_();

		locn.first = NULL;
		locn.second = NULL;

		DbObjPtr ptr;
		ptr.reset(new DbObj);
		write_image(key, ptr);

		size_t ksize = ptr->getSize();

		uint32_t idx = sdb_hashing::hash_fun(ptr->getData(), ptr->getSize() )
		% sfh_.directorySize;

		locn.first = entry_[idx];

		if (entry_[idx] == NULL) {
			return false;
		} else {
			int i = 0;
			string_chain* sa = entry_[idx];
			char* p = sa->str;

			while ( sa ) {
				locn.first = sa;
				//cout<<"search level: "<<sa->level<<endl;
				p = sa->str;
				for (i=0; i<sa->num; i++) {
					size_t ksz, vsz;
					memcpy(&ksz, p, sizeof(size_t));
					p += sizeof(size_t);
					memcpy(&vsz, p, sizeof(size_t));
					p += sizeof(size_t);

					//cout<<ksz<<endl;
					//cout<<vsz<<endl;
					
					if (ksz != ksize) {
						p += ksz + vsz;
						continue;
					}

					
					char *pd = (char *)ptr->getData();
					size_t j=0;
					for (; j<ksz; j++) {
						if (pd[j] != p[j]) {
							break;
						}
					}			
				
					if (j == ksz) {
						locn.second = p-2*sizeof(size_t);
						//cout<<key<<" found"<<endl;
						return true;
					}
					p += ksz + vsz;
				}
				sa = sa->loadNext(dataFile_);				
			}
			locn.second = p;
		}
		return false;
	}

	NodeKeyLocn get_first_Locn()
	{
		NodeKeyLocn locn;
		for(size_t i=0; i<sfh_.directorySize; i++)
		{
			if( entry_[i] )
			{
				locn.first = entry_[i];
				locn.second = entry_[i]->str;
				break;
			}
		}
		return locn;
	}

	bool get(const NodeKeyLocn& locn, DataType& rec) {

		string_chain* sa = locn.first;
		char* p = locn.second;

		if(sa == NULL)return false;
		if(p == NULL)return false;

		size_t ksize, vsize;
		DbObjPtr ptr, ptr1;
		KeyType key;
		ValueType val;

		memcpy(&ksize, p, sizeof(size_t));
		p += sizeof(size_t);
		memcpy(&vsize, p, sizeof(size_t));
		p += sizeof(size_t);

		ptr.reset(new DbObj(p, ksize));
		read_image(key, ptr);
		p += ksize;
		ptr1.reset(new DbObj(p, vsize));
		read_image(val, ptr1);
		p += vsize;

		rec.key = key;
		rec.value = val;

		return true;

	}

	bool seq(NodeKeyLocn& locn, DataType& rec, ESeqDirection sdir=ESD_FORWARD) {

		flushCache_();

		if( sdir == ESD_FORWARD ) {
			string_chain* sa = locn.first;
			char* p = locn.second;

			if(sa == NULL)return false;
			if(p == NULL)return false;

			size_t ksize, vsize;
			DbObjPtr ptr, ptr1;
			KeyType key;
			ValueType val;

			memcpy(&ksize, p, sizeof(size_t));
			p += sizeof(size_t);
			memcpy(&vsize, p, sizeof(size_t));
			p += sizeof(size_t);

			ptr.reset(new DbObj(p, ksize));
			read_image(key, ptr);
			p += ksize;
			ptr1.reset(new DbObj(p, vsize));
			read_image(val, ptr1);
			p += vsize;

			//cout<<"!!!! seq "<<key<<endl;
			rec.key = key;
			rec.value = val;

			memcpy(&ksize, p, sizeof(size_t));	
			if( ksize == 0 ) {
				sa = sa->loadNext(dataFile_);
				if( sa ) {					
					p = sa->str;
				}
				else
				{
					uint32_t idx = sdb_hashing::hash_fun(ptr->getData(), ptr->getSize() )
					% sfh_.directorySize;

					while( !entry_[++idx] ) {
						//cout<<"idx="<<idx<<endl;
						if( idx >= sfh_.directorySize )
						return false;
					}

					//get next bucket;
					sa = entry_[idx];
					p = sa->str;
				}
			}
			locn.first = sa;
			locn.second = p;

			return true;
		} else
		{
			//it seems unecessary, for items are unordered.
			return false;
		}
	}

	int num_items() {
		return sfh_.numItems;
	}

public:

	bool open() {		
		struct stat statbuf;
		bool creating = stat(fileName_.c_str(), &statbuf);

		dataFile_ = fopen(fileName_.c_str(), creating ? "w+b" : "r+b");
		if ( 0 == dataFile_) {
			cout<<"Error in open(): open file failed"<<endl;
			return false;
		}
		bool ret = false;
		if (creating) {
			
		// We're creating if the file doesn't exist.			
#ifdef DEBUG
			cout<<"creating...\n"<<endl;
			sfh_.display();
#endif
			bucketAddr = new long[sfh_.directorySize];
			entry_ = new string_chain*[sfh_.directorySize];

			for(size_t i=0; i<sfh_.directorySize; i++)
			bucketAddr[i] = 0;
			sfh_.toFile(dataFile_);
			ret = true;
		} else {
			if ( !sfh_.fromFile(dataFile_) ) {
				return false;
			} else {
				if (sfh_.magic != 0x061561) {
					cout<<"Error, read wrong file header_\n"<<endl;
					return false;
				}
#ifdef DEBUG
				cout<<"open exist...\n"<<endl;
				sfh_.display();
#endif
				bucketAddr = new long[sfh_.directorySize];
				entry_ = new string_chain*[sfh_.directorySize];

				if (sfh_.directorySize != fread(bucketAddr, sizeof(long),
								sfh_.directorySize, dataFile_))
				return false;
				for (size_t i=0; i<sfh_.directorySize; i++) {
					if (bucketAddr[i] != 0) {
						//cout<<"bucket addr: "<<bucketAddr[i]<<endl;
						entry_[i] = new string_chain(sfh_.bucketSize);
						entry_[i]->fpos = bucketAddr[i];
						entry_[i]->read(dataFile_);					
					}
				}
				ret = true;
			}
		}
		return ret;
	}

	bool close()
	{
		flush();
		for (size_t i=0; i<sfh_.directorySize; i++) {
			string_chain* sc = entry_[i];
			while ( entry_[i] ) {
				sc = entry_[i]->next;
				delete entry_[i];
				entry_[i] = 0;
				entry_[i] = sc;
			}
		}
		delete [] bucketAddr;
		delete entry_;
		fclose(dataFile_);

		return true;
	}

	void flush() {
		sfh_.toFile(dataFile_);
		if (sfh_.directorySize != fwrite(bucketAddr, sizeof(long),
						sfh_.directorySize, dataFile_) )
		return;
		for (size_t i=0; i<sfh_.directorySize; i++) {
			string_chain* sc = entry_[i];
			while (sc) {
				if (sc->write(dataFile_) ) {
					sc = sc->next;
				} else {
					assert(0);
				}
			}
		}
		fflush(dataFile_);
	}

	void display(std::ostream& os = std::cout) {	
		sfh_.display(os);		
		os<<"activeNum: "<<string_chain::activeNum<<endl;
		for (size_t i=0; i<sfh_.directorySize; i++) {
			os<<"["<<i<<"]: ";
			if (entry_[i])
			entry_[i]->display(os);
			os<<endl;			
		}
	}

	/**	 
	 * 
	 *    It displays how much space has been wasted after deleting or updates.   
	 * 
	 *    when an item is deleted, we don't release its space in disk but set a flag that
	 *    it have been deleted. And it will lead to low efficiency. Maybe we should dump it 
	 * 	  to another files when loadFactor are low.
	 * 
	 */
	double loadFactor()
	{
		int nslot = 0;
		for (size_t i=0; i<sfh_.directorySize; i++) {
			string_chain* sc = entry_[i];
			while (sc) {
				nslot += sc->num;
				sc = sc->next;
			}
		}
		if(nslot == 0 )return 0.0;
		else
		return sfh_.numItems/nslot;

	}

protected:
	string_chain** entry_;
	long *bucketAddr;

	multimap<int, string_chain*, greater<int> > sh_cache_;
	typedef typename multimap<int, string_chain*, greater<int> >::iterator CacheIter;

private:
	ShFileHeader sfh_;
	string fileName_;
	FILE* dataFile_;

private:
	string_chain* allocateBlock_() {
		//cout<<"allocateBlock idx="<<sfh_.nBlock<<endl;
		string_chain* newBlock;
		newBlock = new string_chain(sfh_.bucketSize);
		newBlock->str = new char[sfh_.bucketSize-sizeof(long)-sizeof(int)];
		memset(newBlock->str, 0, sfh_.bucketSize-sizeof(long)-sizeof(int));
		newBlock->isLoaded = true;
		newBlock->isDirty = true;

		newBlock->fpos = sizeof(ShFileHeader) + sizeof(long)*sfh_.directorySize + sfh_.bucketSize*sfh_.nBlock;
		sfh_.nBlock++;		
		
		string_chain::activeNum++;

		return newBlock;
	}

	void flushCache_()
	{

		//cout<<string_chain::activeNum<<" vs "<<sfh_.cacheSize <<endl;
		if( string_chain::activeNum >  sfh_.cacheSize )
		{
#ifdef DEBUG
			cout<<"cache is full..."<<endl;
			cout<<string_chain::activeNum<<" vs "<<sfh_.cacheSize <<endl;
#endif

			
			for(size_t i=0; i<sfh_.directorySize; i++){
				string_chain* sc = entry_[i];
				while( sc ){
					//if(  sc->level>0 && (size_t)sc->level > sfh_.cacheSize/sfh_.directorySize/2 - 1 )
					if( sc->level>0 )
					{
						if(sc->isLoaded)
							sh_cache_.insert(make_pair(sc->level, sc ));			
					}
					sc = sc->next;
				}						
			}	
			
			//cout<<sh_cache_.size()<<endl;				
			for(CacheIter it = sh_cache_.begin(); it != sh_cache_.end(); it++ )
			 {
#ifdef DEBUG
				//display cache
				/*cout<<"(level: "<<it->second->level;
				cout<<"  val:  "<<it->second;
				cout<<"  fpos: "<<it->second->fpos;	
				cout<<"  num: "<<it->second->num;		
				cout<<" )-> ";*/
#endif				
				it->second->write(dataFile_);
				it->second->unload();
				if( string_chain::activeNum < sfh_.cacheSize/2 ){
					fflush(dataFile_);
					sh_cache_.clear();	
					
					//cout<<" !!!! "<<string_chain::activeNum<<" vs "<<sfh_.cacheSize <<endl;
					//display();
					return;
				}
			 }		
			fflush(dataFile_);
			sh_cache_.clear();				
			//cout<<" !!!! "<<string_chain::activeNum<<" vs "<<sfh_.cacheSize <<endl;
			//display();	
		}
	}
};

NS_IZENELIB_AM_END

#endif
