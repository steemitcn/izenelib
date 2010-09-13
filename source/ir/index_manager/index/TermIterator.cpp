#include <ir/index_manager/index/TermIterator.h>
#include <ir/index_manager/index/TermInfo.h>
#include <ir/index_manager/index/InputDescriptor.h>
#include <ir/index_manager/index/TermReader.h>

#include <sstream>

using namespace std;

NS_IZENELIB_IR_BEGIN

namespace indexmanager{

TermIterator::TermIterator()
{}

TermIterator::~TermIterator()
{}

VocIterator::VocIterator(VocReader* termReader)
        :pTermReader_(termReader)
        ,pCurTerm_(NULL)
        ,pCurTermInfo_(NULL)
        ,pCurTermPosting_(NULL)
        ,pInputDescriptor_(NULL)
        ,nCurPos_(-1)
{
}

VocIterator::~VocIterator(void)
{
    if (pCurTerm_)
    {
        delete pCurTerm_;
        pCurTerm_ = NULL;
    }
    if (pCurTermPosting_)
    {
        delete pCurTermPosting_;
        pCurTermPosting_ = NULL;
    }
    if (pInputDescriptor_)
    {
        delete pInputDescriptor_;
        pInputDescriptor_ = NULL;
    }
    pCurTermInfo_ = NULL;
}

const Term* VocIterator::term()
{
    return pCurTerm_;
}

const TermInfo* VocIterator::termInfo()
{
    return pCurTermInfo_;
}

PostingReader* VocIterator::termPosting()
{
    if (!pCurTermPosting_)
    {
        IndexInput* pInput;
        pInputDescriptor_ = new InputDescriptor(true);
        pInput = pTermReader_->getTermReaderImpl()->pInputDescriptor_->getDPostingInput()->clone();
        pInputDescriptor_->setDPostingInput(pInput);
        pInput = pTermReader_->getTermReaderImpl()->pInputDescriptor_->getPPostingInput()->clone();
        pInputDescriptor_->setPPostingInput(pInput);
        pCurTermPosting_ = new RTDiskPostingReader(skipInterval_, maxSkipLevel_, pInputDescriptor_,*pCurTermInfo_);
    }
    else
    {
        ((RTDiskPostingReader*)pCurTermPosting_)->reset(*pCurTermInfo_);///reset to a new posting
    }
    return pCurTermPosting_;
}

bool VocIterator::next()
{
    if(pTermReader_->getTermReaderImpl()->nTermCount_ > nCurPos_+1)			
    {
        if(pCurTerm_ == NULL)
            pCurTerm_ = new Term(pTermReader_->getFieldInfo()->getName(),pTermReader_->getTermReaderImpl()->pTermTable_[++nCurPos_].tid);
        else 
            pCurTerm_->setValue(pTermReader_->getTermReaderImpl()->pTermTable_[++nCurPos_].tid);
        pCurTermInfo_ = &(pTermReader_->getTermReaderImpl()->pTermTable_[nCurPos_].ti);
        return true;
    }
    else return false;
}

//////////////////////////////////////////////////////////////////////////
///RTDiskTermIterator
RTDiskTermIterator::RTDiskTermIterator(Directory* pDirectory,const char* barrelname,FieldInfo* pFieldInfo)
    :pDirectory_(pDirectory)
    ,pFieldInfo_(pFieldInfo)
    ,pCurTerm_(NULL)
    ,pCurTermInfo_(NULL)
    ,pCurTermPosting_(NULL)
    ,pInputDescriptor_(NULL)
    ,nCurPos_(-1)
{
    barrelName_ = barrelname;
    pVocInput_ = pDirectory->openInput(barrelName_ + ".voc");
    pVocInput_->seek(pFieldInfo->getIndexOffset());
    fileoffset_t voffset = pVocInput_->getFilePointer();
    ///begin read vocabulary descriptor
    nVocLength_ = pVocInput_->readLong();
    nTermCount_ = (int32_t)pVocInput_->readLong(); ///get total term count
    ///end read vocabulary descriptor
    pVocInput_->seek(voffset - nVocLength_);///seek to begin of vocabulary data

}

RTDiskTermIterator::~RTDiskTermIterator()
{
    delete pVocInput_;
    if (pCurTerm_)
    {
        delete pCurTerm_;
        pCurTerm_ = NULL;
    }
    if (pCurTermPosting_)
    {
        delete pCurTermPosting_;
        pCurTermPosting_ = NULL;
    }
    if(pCurTermInfo_)
    {
        delete pCurTermInfo_;
        pCurTermInfo_ = NULL;
    }
}

const Term* RTDiskTermIterator::term()
{
    return pCurTerm_;
}

const TermInfo* RTDiskTermIterator::termInfo()
{
    return pCurTermInfo_;
}

PostingReader* RTDiskTermIterator::termPosting()
{
    if (!pCurTermPosting_)
    {
        pInputDescriptor_ = new InputDescriptor(true);
        pInputDescriptor_->setDPostingInput(pDirectory_->openInput(barrelName_ + ".dfp"));
        pInputDescriptor_->setPPostingInput(pDirectory_->openInput(barrelName_ + ".pop"));
        pCurTermPosting_ = new RTDiskPostingReader(skipInterval_, maxSkipLevel_, pInputDescriptor_,*pCurTermInfo_);
    }
    else
    {
        ((RTDiskPostingReader*)pCurTermPosting_)->reset(*pCurTermInfo_);///reset to a new posting
    }
    return pCurTermPosting_;
}

bool RTDiskTermIterator::next()
{
    if(nTermCount_ > nCurPos_+1) 		
    {
        ++nCurPos_;
        termid_t tid = pVocInput_->readInt();
        freq_t df = pVocInput_->readInt();
        freq_t ctf = pVocInput_->readInt();
        docid_t lastdoc = pVocInput_->readInt();
        freq_t skipLevel = pVocInput_->readInt();
        fileoffset_t skipPointer = pVocInput_->readLong();
        fileoffset_t docPointer = pVocInput_->readLong();
        freq_t docPostingLen = pVocInput_->readInt();
        fileoffset_t positionPointer = pVocInput_->readLong();
        freq_t positionPostingLen = pVocInput_->readInt();	

        if(pCurTerm_ == NULL)
            pCurTerm_ = new Term(pFieldInfo_->getName(),tid);
        else 
            pCurTerm_->setValue(tid);
        if(pCurTermInfo_ == NULL)
            pCurTermInfo_ = new TermInfo();
        pCurTermInfo_->set(df,ctf,lastdoc,skipLevel,skipPointer,docPointer,docPostingLen,positionPointer,positionPostingLen);;
        return true;
    }
    else return false;
}


//////////////////////////////////////////////////////////////////////////
//
MemTermIterator::MemTermIterator(MemTermReader* pTermReader)
        :pTermReader_(pTermReader)
        ,pCurTerm_(NULL)
        ,pCurTermInfo_(NULL)
        ,pCurTermPosting_(NULL)
        ,postingIterator_(pTermReader->pIndexer_->postingMap_.begin())
        ,postingIteratorEnd_(pTermReader->pIndexer_->postingMap_.end())
{
}

MemTermIterator::~MemTermIterator(void)
{
    if (pCurTerm_)
    {
        delete pCurTerm_;
        pCurTerm_ = NULL;
    }
    if(pCurTermPosting_)
    {
        delete pCurTermPosting_;
        pCurTermPosting_ = NULL;
    }
    if (pCurTermInfo_)
    {
        delete pCurTermInfo_;
        pCurTermInfo_ = NULL;
    }
}

bool MemTermIterator::next()
{
    postingIterator_++;
    RTPostingWriter* pPostingWriter = NULL;
    if(postingIterator_ != postingIteratorEnd_)
    {
        pPostingWriter = postingIterator_->second;
        while (pPostingWriter->isEmpty())
        {
            postingIterator_++;
            if(postingIterator_ != postingIteratorEnd_)
                pPostingWriter = postingIterator_->second;
            else return false;
        }

        //TODO
        if (pCurTerm_ == NULL)
            pCurTerm_ = new Term(pTermReader_->field_.c_str(),postingIterator_->first);
        else pCurTerm_->setValue(postingIterator_->first);
        pPostingWriter = postingIterator_->second;

        if(pCurTermPosting_)
        {
            delete pCurTermPosting_;
            pCurTermPosting_ = NULL;
        }

        pCurTermPosting_ = (MemPostingReader*)pPostingWriter->createPostingReader();
        if (pCurTermInfo_ == NULL)
            pCurTermInfo_ = new TermInfo();
        pCurTermInfo_->set(
                                pCurTermPosting_->docFreq(),
                                pCurTermPosting_->getCTF(),
                                pCurTermPosting_->lastDocID(),
                                pCurTermPosting_->getSkipLevel(),
                                -1,-1,0,-1,0);
        return true;
    }
    else return false;
}

const Term* MemTermIterator::term()
{
    return pCurTerm_;
}
const TermInfo* MemTermIterator::termInfo()
{
    return pCurTermInfo_;
}
PostingReader* MemTermIterator::termPosting()
{
    return pCurTermPosting_;
}

}

NS_IZENELIB_IR_END

