#ifndef _FM_INDEX_WAVELET_MATRIX_HPP
#define _FM_INDEX_WAVELET_MATRIX_HPP

#include "wavelet_tree.hpp"
#include "utils.hpp"
#include <am/succinct/sdarray/SDArray.hpp>
#include <vector>

NS_IZENELIB_AM_BEGIN

namespace succinct
{
namespace fm_index
{

template <class CharT>
class WaveletMatrix : public WaveletTree<CharT>
{
public:
    typedef CharT char_type;
    typedef WaveletMatrix<CharT> self_type;

    WaveletMatrix(size_t alphabet_num, bool support_select, bool dense);
    ~WaveletMatrix();

    void build(const char_type *char_seq, size_t len);

    char_type access(size_t pos) const;
    char_type access(size_t pos, size_t &rank) const;

    size_t rank(char_type c, size_t pos) const;
    size_t select(char_type c, size_t rank) const;

    void intersect(
            const std::vector<std::pair<size_t, size_t> > &ranges,
            size_t thres,
            size_t max_count,
            std::vector<char_type> &results) const;

    void topKUnion(
            const std::vector<boost::tuple<size_t, size_t, double> > &ranges,
            size_t topK,
            std::vector<std::pair<double, char_type> > &results) const;

    void topKUnionWithFilters(
            const std::vector<std::pair<size_t, size_t> > &filters,
            const std::vector<boost::tuple<size_t, size_t, double> > &ranges,
            size_t topK,
            std::vector<std::pair<double, char_type> > &results) const;

    void topKUnionWithAuxFilters(
            const std::vector<FilterList<self_type> *> &aux_filters,
            const std::vector<boost::tuple<size_t, size_t, double> > &ranges,
            size_t topK,
            std::vector<std::pair<double, char_type> > &results) const;

    size_t getOcc(char_type c) const;
    WaveletTreeNode *getRoot() const;

    size_t length() const;
    size_t allocSize() const;

    void save(std::ostream &ostr) const;
    void load(std::istream &istr);

private:
    void doIntersect_(
            const std::vector<std::pair<size_t, size_t> > &ranges,
            size_t thres,
            size_t max_count,
            size_t level,
            char_type symbol,
            std::vector<char_type> &results) const;

private:
    sdarray::SDArray occ_;
    std::vector<size_t> zero_counts_;
    std::vector<WaveletTreeNode *> nodes_;
};

template <class CharT>
WaveletMatrix<CharT>::WaveletMatrix(uint64_t alphabet_num, bool support_select, bool dense)
    : WaveletTree<CharT>(alphabet_num, support_select, dense)
{
}

template <class CharT>
WaveletMatrix<CharT>::~WaveletMatrix()
{
    for (size_t i = 0; i < nodes_.size(); ++i)
    {
        if (nodes_[i]) delete nodes_[i];
    }
}

template <class CharT>
void WaveletMatrix<CharT>::build(const char_type *char_seq, size_t len)
{
    if (this->alphabet_num_ == 0) return;

    this->alphabet_bit_num_ = bits(this->alphabet_num_ - 1);

    zero_counts_.resize(this->alphabet_bit_num_);

    nodes_.resize(this->alphabet_bit_num_);

    std::vector<size_t> prev_begin_pos(1), node_begin_pos(1);
    char_type bit_mask, subscript;

    for (size_t i = 0; i < this->alphabet_bit_num_; ++i)
    {
        node_begin_pos.clear();
        node_begin_pos.resize((1ULL << (i + 1)) + 1);

        nodes_[i] = new WaveletTreeNode(this->support_select_, this->dense_);
        nodes_[i]->resize(len);

        bit_mask = (char_type)1 << i;

        for (size_t j = 0; j < len; ++j)
        {
            subscript = char_seq[j] & (bit_mask - 1);

            if (char_seq[j] & bit_mask)
            {
                nodes_[i]->setBit(prev_begin_pos[subscript]++);
                ++node_begin_pos[subscript + bit_mask + 1];
            }
            else
            {
                nodes_[i]->unsetBit(prev_begin_pos[subscript]++);
                ++node_begin_pos[subscript + 1];
            }
        }

        nodes_[i]->build();

        prev_begin_pos = node_begin_pos;
        for (size_t j = 2; j < prev_begin_pos.size(); ++j)
        {
            prev_begin_pos[j] += prev_begin_pos[j - 1];
        }
        zero_counts_[i] = prev_begin_pos[bit_mask];
    }

    for (size_t i = 1; i <= this->alphabet_num_; ++i)
    {
        occ_.add(node_begin_pos[i]);
    }
    occ_.build();

    for (size_t i = 1; i < nodes_.size(); ++i)
    {
        nodes_[i]->parent_ = nodes_[i - 1];
        nodes_[i - 1]->left_ = nodes_[i];
        nodes_[i - 1]->right_ = nodes_[i];
    }
}

template <class CharT>
CharT WaveletMatrix<CharT>::access(size_t pos) const
{
    if (pos >= length()) return -1;

    char_type c = 0;
    char_type bit_mask = 1;

    for (size_t i = 0; i < nodes_.size(); ++i)
    {
        if (nodes_[i]->access(pos, pos))
        {
            c |= bit_mask;
            pos += zero_counts_[i];
        }

        bit_mask <<= 1;
    }

    return c;
}

template <class CharT>
CharT WaveletMatrix<CharT>::access(size_t pos, size_t &rank) const
{
    if (pos >= length()) return -1;

    char_type c = 0;
    char_type bit_mask = 1;

    for (size_t i = 0; i < nodes_.size(); ++i)
    {
        if (nodes_[i]->access(pos, pos))
        {
            c |= bit_mask;
            pos += zero_counts_[i];
        }

        bit_mask <<= 1;
    }

    rank = pos - occ_.prefixSum(c);

    return c;
}

template <class CharT>
size_t WaveletMatrix<CharT>::rank(char_type c, size_t pos) const
{
    pos = std::min(pos, length());

    char_type bit_mask = 1;

    for (size_t i = 0; i < nodes_.size(); ++i)
    {
        if (c & bit_mask)
        {
            pos = nodes_[i]->rank1(pos) + zero_counts_[i];
        }
        else
        {
            pos -= nodes_[i]->rank1(pos);
        }

        bit_mask <<= 1;
    }

    return pos - occ_.prefixSum(c);
}

template <class CharT>
size_t WaveletMatrix<CharT>::select(char_type c, size_t rank) const
{
    size_t pos = rank + occ_.prefixSum(c);

    for (size_t i = nodes_.size() - 1; i < nodes_.size(); --i)
    {
        if (pos >= zero_counts_[i])
        {
            pos = nodes_[i]->select1(pos - zero_counts_[i]);
        }
        else
        {
            pos = nodes_[i]->select0(pos);
        }
        //pos++;
        if (pos == (size_t)-1) return -1;
    }

    return pos;
}

template <class CharT>
void WaveletMatrix<CharT>::intersect(
        const std::vector<std::pair<size_t, size_t> > &ranges,
        size_t thres,
        size_t max_count,
        std::vector<char_type> &results) const
{
    if (thres > ranges.size()) return;
    if (thres > 0) thres = ranges.size() - thres;

    doIntersect_(ranges, thres, max_count, 0, 0, results);
}

template <class CharT>
void WaveletMatrix<CharT>::doIntersect_(
        const std::vector<std::pair<size_t, size_t> > &ranges,
        size_t thres,
        size_t max_count,
        size_t level,
        char_type symbol,
        std::vector<char_type> &results) const
{
    if (results.size() >= max_count) return;

    if (level == this->alphabet_bit_num_)
    {
        results.push_back(symbol);
        return;
    }

    std::vector<std::pair<size_t, size_t> > zero_ranges, one_ranges;
    zero_ranges.reserve(ranges.size());
    one_ranges.reserve(ranges.size());

    size_t zero_thres = thres, one_thres = thres;
    bool has_zeros = true, has_ones = true;

    const WaveletTreeNode *node = nodes_[level];

    size_t rank_start, rank_end;

    for (std::vector<std::pair<size_t, size_t> >::const_iterator it = ranges.begin();
            it != ranges.end(); ++it)
    {
        rank_start = node->rank1(it->first);
        rank_end = node->rank1(it->second);

        if (has_zeros)
        {
            if (it->first - rank_start >= it->second - rank_end)
            {
                if (zero_thres == 0)
                {
                    if (!has_ones) return;
                    has_zeros = false;
                }
                else
                {
                    --zero_thres;
                }
            }
            else
            {
                zero_ranges.push_back(std::make_pair(it->first - rank_start, it->second - rank_end));
            }
        }

        if (has_ones)
        {
            if (rank_start >= rank_end)
            {
                if (one_thres == 0)
                {
                    if (!has_zeros) return;
                    has_ones = false;
                }
                else
                {
                    --one_thres;
                }
            }
            else
            {
                one_ranges.push_back(std::make_pair(rank_start + zero_counts_[level], rank_end + zero_counts_[level]));
            }
        }
    }

    if (has_zeros)
    {
        doIntersect_(zero_ranges, zero_thres, max_count, level + 1, symbol, results);
    }

    if (has_ones)
    {
        symbol |= (char_type)1 << level;

        doIntersect_(one_ranges, one_thres, max_count, level + 1, symbol, results);
    }
}

template <class CharT>
void WaveletMatrix<CharT>::topKUnion(
        const std::vector<boost::tuple<size_t, size_t, double> > &ranges,
        size_t topK,
        std::vector<std::pair<double, char_type> > &results) const
{
    if (topK == 0) return;

    boost::priority_deque<PatternList *> ranges_queue;
    ranges_queue.push(new PatternList(0, (char_type)0, nodes_[0], ranges));

    if (ranges_queue.top()->score_ == 0.0)
    {
        delete ranges_queue.top();
        return;
    }

    results.reserve(topK);

    size_t max_queue_size = std::max(topK, DEFAULT_TOP_K);

    std::vector<PatternList *> recyc_ranges;
    recyc_ranges.reserve(max_queue_size + 2);
    for (size_t i = 0; i < max_queue_size + 1; ++i)
    {
        recyc_ranges.push_back(new PatternList(0, 0, NULL, ranges.size()));
    }

    PatternList *top_ranges;
    PatternList *zero_ranges, *one_ranges;
    size_t level, rank_start, rank_end, zero_end;
    const WaveletTreeNode *node;

    while (!ranges_queue.empty() && results.size() < topK)
    {
        top_ranges = ranges_queue.top();
        ranges_queue.pop_top();

        if (!top_ranges->node_)
        {
            results.push_back(std::make_pair(top_ranges->score_, top_ranges->sym_));
            recyc_ranges.push_back(top_ranges);
            continue;
        }

        level = top_ranges->level_;
        zero_end = zero_counts_[level];
        node = top_ranges->node_;

        zero_ranges = recyc_ranges.back();
        zero_ranges->reset(level + 1, top_ranges->sym_, node->left_);
        recyc_ranges.pop_back();

        one_ranges = recyc_ranges.back();
        one_ranges->reset(level + 1, top_ranges->sym_ | (char_type)1 << level, node->right_);
        recyc_ranges.pop_back();

        for (std::vector<boost::tuple<size_t, size_t, double> >::const_iterator it = top_ranges->patterns_.begin();
                it != top_ranges->patterns_.end(); ++it)
        {
            rank_start = node->rank1(it->get<0>());
            rank_end = node->rank1(it->get<1>());

            zero_ranges->addPattern(boost::make_tuple(it->get<0>() - rank_start, it->get<1>() - rank_end, it->get<2>()));
            one_ranges->addPattern(boost::make_tuple(rank_start + zero_end, rank_end + zero_end, it->get<2>()));
        }

        recyc_ranges.push_back(top_ranges);

        zero_ranges->calcScore();
        if (zero_ranges->score_ == 0.0)
        {
            recyc_ranges.push_back(zero_ranges);
        }
        else if (!zero_ranges->node_ && (ranges_queue.empty() || zero_ranges->score_ >= ranges_queue.top()->score_))
        {
            results.push_back(std::make_pair(zero_ranges->score_, zero_ranges->sym_));
            recyc_ranges.push_back(zero_ranges);
        }
        else
        {
            ranges_queue.push(zero_ranges);
        }

        one_ranges->calcScore();
        if (one_ranges->score_ == 0.0 || (ranges_queue.size() >= max_queue_size && one_ranges->score_ < ranges_queue.bottom()->score_))
        {
            recyc_ranges.push_back(one_ranges);
        }
        else if (!one_ranges->node_ && (ranges_queue.empty() || one_ranges->score_ >= ranges_queue.top()->score_))
        {
            results.push_back(std::make_pair(one_ranges->score_, one_ranges->sym_));
            recyc_ranges.push_back(one_ranges);
        }
        else
        {
            ranges_queue.push(one_ranges);

            if (ranges_queue.size() > max_queue_size)
            {
                recyc_ranges.push_back(ranges_queue.bottom());
                ranges_queue.pop_bottom();
            }
        }
    }

    for (size_t i = 0; i < ranges_queue.size(); ++i)
    {
        delete ranges_queue.get(i);
    }

    for (size_t i = 0; i < recyc_ranges.size(); ++i)
    {
        delete recyc_ranges[i];
    }
}

template <class CharT>
void WaveletMatrix<CharT>::topKUnionWithFilters(
        const std::vector<std::pair<size_t, size_t> > &filters,
        const std::vector<boost::tuple<size_t, size_t, double> > &ranges,
        size_t topK,
        std::vector<std::pair<double, char_type> > &results) const
{
    if (topK == 0) return;

    boost::priority_deque<FilteredPatternList *> ranges_queue;
    ranges_queue.push(new FilteredPatternList(0, (char_type)0, nodes_[0], filters, ranges));

    if (ranges_queue.top()->score_ == 0.0)
    {
        delete ranges_queue.top();
        return;
    }

    results.reserve(topK);

    size_t max_queue_size = std::max(topK, DEFAULT_TOP_K);

    std::vector<FilteredPatternList *> recyc_ranges;
    recyc_ranges.reserve(max_queue_size + 2);
    for (size_t i = 0; i < max_queue_size + 1; ++i)
    {
        recyc_ranges.push_back(new FilteredPatternList(0, 0, NULL, filters.size(), ranges.size()));
    }

    FilteredPatternList *top_ranges;
    FilteredPatternList *zero_ranges, *one_ranges;
    size_t level, rank_start, rank_end, zero_end;
    const WaveletTreeNode *node;

    while (!ranges_queue.empty() && results.size() < topK)
    {
        top_ranges = ranges_queue.top();
        ranges_queue.pop_top();

        if (!top_ranges->node_)
        {
            results.push_back(std::make_pair(top_ranges->score_, top_ranges->sym_));
            recyc_ranges.push_back(top_ranges);
            continue;
        }

        level = top_ranges->level_;
        zero_end = zero_counts_[level];
        node = top_ranges->node_;

        zero_ranges = recyc_ranges.back();
        zero_ranges->reset(level + 1, top_ranges->sym_, node->left_);
        recyc_ranges.pop_back();

        one_ranges = recyc_ranges.back();
        one_ranges->reset(level + 1, top_ranges->sym_ | (char_type)1 << level, node->right_);
        recyc_ranges.pop_back();

        for (std::vector<std::pair<size_t, size_t> >::const_iterator it = top_ranges->filters_.begin();
                it != top_ranges->filters_.end(); ++it)
        {
            rank_start = node->rank1(it->first);
            rank_end = node->rank1(it->second);

            zero_ranges->addFilter(std::make_pair(it->first - rank_start, it->second - rank_end));
            one_ranges->addFilter(std::make_pair(rank_start + zero_end, rank_end + zero_end));
        }

        if (zero_ranges->filters_.empty())
        {
            recyc_ranges.push_back(zero_ranges);
            zero_ranges = NULL;
        }
        if (one_ranges->filters_.empty())
        {
            recyc_ranges.push_back(one_ranges);
            one_ranges = NULL;
        }
        if (!zero_ranges && !one_ranges)
        {
            recyc_ranges.push_back(top_ranges);
            continue;
        }

        for (std::vector<boost::tuple<size_t, size_t, double> >::const_iterator it = top_ranges->patterns_.begin();
                it != top_ranges->patterns_.end(); ++it)
        {
            rank_start = node->rank1(it->get<0>());
            rank_end = node->rank1(it->get<1>());

            if (zero_ranges)
            {
                zero_ranges->addPattern(boost::make_tuple(it->get<0>() - rank_start, it->get<1>() - rank_end, it->get<2>()));
            }
            if (one_ranges)
            {
                one_ranges->addPattern(boost::make_tuple(rank_start + zero_end, rank_end + zero_end, it->get<2>()));
            }
        }

        recyc_ranges.push_back(top_ranges);

        if (zero_ranges)
        {
            zero_ranges->calcScore();
            if (zero_ranges->score_ == 0.0)
            {
                recyc_ranges.push_back(zero_ranges);
            }
            else if (!zero_ranges->node_ && (ranges_queue.empty() || zero_ranges->score_ >= ranges_queue.top()->score_))
            {
                results.push_back(std::make_pair(zero_ranges->score_, zero_ranges->sym_));
                recyc_ranges.push_back(zero_ranges);
            }
            else
            {
                ranges_queue.push(zero_ranges);
            }
        }

        if (one_ranges)
        {
            one_ranges->calcScore();
            if (one_ranges->score_ == 0.0 || (ranges_queue.size() >= max_queue_size && one_ranges->score_ < ranges_queue.bottom()->score_))
            {
                recyc_ranges.push_back(one_ranges);
            }
            else if (!one_ranges->node_ && (ranges_queue.empty() || one_ranges->score_ >= ranges_queue.top()->score_))
            {
                results.push_back(std::make_pair(one_ranges->score_, one_ranges->sym_));
                recyc_ranges.push_back(one_ranges);
            }
            else
            {
                ranges_queue.push(one_ranges);

                if (ranges_queue.size() > max_queue_size)
                {
                    recyc_ranges.push_back(ranges_queue.bottom());
                    ranges_queue.pop_bottom();
                }
            }
        }
    }

    for (size_t i = 0; i < ranges_queue.size(); ++i)
    {
        delete ranges_queue.get(i);
    }

    for (size_t i = 0; i < recyc_ranges.size(); ++i)
    {
        delete recyc_ranges[i];
    }
}

template <class CharT>
void WaveletMatrix<CharT>::topKUnionWithAuxFilters(
        const std::vector<FilterList<self_type> *> &aux_filters,
        const std::vector<boost::tuple<size_t, size_t, double> > &ranges,
        size_t topK,
        std::vector<std::pair<double, char_type> > &results) const
{
    if (topK == 0) return;

    boost::priority_deque<AuxFilteredPatternList<self_type> *> ranges_queue;
    ranges_queue.push(new AuxFilteredPatternList<self_type>(0, (char_type)0, nodes_[0], aux_filters, ranges));

    if (ranges_queue.top()->score_ == 0.0)
    {
        delete ranges_queue.top();
        return;
    }

    results.reserve(topK);

    size_t max_queue_size = std::max(topK, DEFAULT_TOP_K);
    size_t max_filter_size = 0;
    for (size_t i = 0; i < aux_filters.size(); ++i)
    {
        max_filter_size = std::max(max_filter_size, aux_filters[i]->filters_.size());
    }

    std::vector<AuxFilteredPatternList<self_type> *> recyc_ranges;
    recyc_ranges.reserve(max_queue_size + 2);
    for (size_t i = 0; i < max_queue_size + 1; ++i)
    {
        recyc_ranges.push_back(new AuxFilteredPatternList<self_type>(0, 0, NULL, aux_filters.size(), ranges.size(), max_filter_size));
    }

    AuxFilteredPatternList<self_type> *top_ranges;
    AuxFilteredPatternList<self_type> *zero_ranges, *one_ranges;
    FilterList<self_type> *zero_filter, *one_filter;
    size_t level, rank_start, rank_end, zero_end;
    const WaveletTreeNode *node;

    while (!ranges_queue.empty() && results.size() < topK)
    {
        top_ranges = ranges_queue.top();
        ranges_queue.pop_top();

        if (!top_ranges->node_)
        {
            results.push_back(std::make_pair(top_ranges->score_, top_ranges->sym_));
            recyc_ranges.push_back(top_ranges);
            continue;
        }

        level = top_ranges->level_;
        node = top_ranges->node_;

        zero_ranges = recyc_ranges.back();
        zero_ranges->reset(level + 1, top_ranges->sym_, node->left_);
        recyc_ranges.pop_back();

        one_ranges = recyc_ranges.back();
        one_ranges->reset(level + 1, top_ranges->sym_ | (char_type)1 << level, node->right_);
        recyc_ranges.pop_back();

        for (typename std::vector<FilterList<self_type> *>::const_iterator it = top_ranges->aux_filters_.begin();
                it != top_ranges->aux_filters_.end(); ++it)
        {
            zero_end = (*it)->tree_->zero_counts_[level];
            node = (*it)->node_;

            if (zero_ranges)
            {
                zero_filter = zero_ranges->recyc_aux_filters_.back();
                zero_filter->reset((*it)->tree_, node->left_);
                zero_ranges->recyc_aux_filters_.pop_back();
            }
            if (one_ranges)
            {
                one_filter = one_ranges->recyc_aux_filters_.back();
                one_filter->reset((*it)->tree_, node->right_);
                one_ranges->recyc_aux_filters_.pop_back();
            }

            for (std::vector<std::pair<size_t, size_t> >::const_iterator fit = (*it)->filters_.begin();
                    fit != (*it)->filters_.end(); ++fit)
            {
                rank_start = node->rank1(fit->first);
                rank_end = node->rank1(fit->second);

                if (zero_ranges)
                {
                    zero_filter->addFilter(std::make_pair(fit->first - rank_start, fit->second - rank_end));
                }
                if (one_ranges)
                {
                    one_filter->addFilter(std::make_pair(rank_start + zero_end, rank_end + zero_end));
                }
            }

            if (zero_ranges && !zero_ranges->addAuxFilter(zero_filter))
            {
                recyc_ranges.push_back(zero_ranges);
                zero_ranges = NULL;
                if (!one_ranges) break;
            }
            if (one_ranges && !one_ranges->addAuxFilter(one_filter))
            {
                recyc_ranges.push_back(one_ranges);
                one_ranges = NULL;
                if (!zero_ranges) break;
            }
        }

        if (!zero_ranges && !one_ranges)
        {
            recyc_ranges.push_back(top_ranges);
            continue;
        }

        zero_end = zero_counts_[level];
        node = top_ranges->node_;

        for (std::vector<boost::tuple<size_t, size_t, double> >::const_iterator it = top_ranges->patterns_.begin();
                it != top_ranges->patterns_.end(); ++it)
        {
            rank_start = node->rank1(it->get<0>());
            rank_end = node->rank1(it->get<1>());

            if (zero_ranges)
            {
                zero_ranges->addPattern(boost::make_tuple(it->get<0>() - rank_start, it->get<1>() - rank_end, it->get<2>()));
            }
            if (one_ranges)
            {
                one_ranges->addPattern(boost::make_tuple(rank_start + zero_end, rank_end + zero_end, it->get<2>()));
            }
        }

        recyc_ranges.push_back(top_ranges);

        if (zero_ranges)
        {
            zero_ranges->calcScore();
            if (zero_ranges->score_ == 0.0)
            {
                recyc_ranges.push_back(zero_ranges);
            }
            else if (!zero_ranges->node_ && (ranges_queue.empty() || zero_ranges->score_ >= ranges_queue.top()->score_))
            {
                results.push_back(std::make_pair(zero_ranges->score_, zero_ranges->sym_));
                recyc_ranges.push_back(zero_ranges);
            }
            else
            {
                ranges_queue.push(zero_ranges);
            }
        }

        if (one_ranges)
        {
            one_ranges->calcScore();
            if (one_ranges->score_ == 0.0 || (ranges_queue.size() >= max_queue_size && one_ranges->score_ < ranges_queue.bottom()->score_))
            {
                recyc_ranges.push_back(one_ranges);
            }
            else if (!one_ranges->node_ && (ranges_queue.empty() || one_ranges->score_ >= ranges_queue.top()->score_))
            {
                results.push_back(std::make_pair(one_ranges->score_, one_ranges->sym_));
                recyc_ranges.push_back(one_ranges);
            }
            else
            {
                ranges_queue.push(one_ranges);

                if (ranges_queue.size() > max_queue_size)
                {
                    recyc_ranges.push_back(ranges_queue.bottom());
                    ranges_queue.pop_bottom();
                }
            }
        }
    }

    for (size_t i = 0; i < ranges_queue.size(); ++i)
    {
        delete ranges_queue.get(i);
    }

    for (size_t i = 0; i < recyc_ranges.size(); ++i)
    {
        delete recyc_ranges[i];
    }
}

template <class CharT>
size_t WaveletMatrix<CharT>::getOcc(char_type c) const
{
    if (c <= occ_.size()) return occ_.prefixSum(c);
    return occ_.getSum();
}

template <class CharT>
WaveletTreeNode *WaveletMatrix<CharT>::getRoot() const
{
    if (nodes_.empty()) return NULL;
    else return nodes_[0];
}

template <class CharT>
size_t WaveletMatrix<CharT>::length() const
{
    return nodes_.empty() ? 0 : nodes_[0]->length();
}

template <class CharT>
size_t WaveletMatrix<CharT>::allocSize() const
{
    size_t sum = sizeof(WaveletMatrix<char_type>) + occ_.allocSize() + sizeof(zero_counts_[0]) * zero_counts_.size();
    for (size_t i = 0; i < nodes_.size(); ++i)
        sum += nodes_[i]->allocSize();

    return sum;
}

template <class CharT>
void WaveletMatrix<CharT>::save(std::ostream &ostr) const
{
    WaveletTree<CharT>::save(ostr);
    occ_.save(ostr);

    ostr.write((const char *)&zero_counts_[0], sizeof(zero_counts_[0]) * zero_counts_.size());

    for (size_t i = 0; i < nodes_.size(); ++i)
    {
        nodes_[i]->save(ostr);
    }
}

template <class CharT>
void WaveletMatrix<CharT>::load(std::istream &istr)
{
    WaveletTree<CharT>::load(istr);
    occ_.load(istr);

    for (size_t i = 0; i < nodes_.size(); ++i)
    {
        if (nodes_[i]) delete nodes_[i];
    }

    this->alphabet_bit_num_ = bits(this->alphabet_num_ - 1);

    zero_counts_.resize(this->alphabet_bit_num_);
    istr.read((char *)&zero_counts_[0], sizeof(zero_counts_[0]) * zero_counts_.size());

    nodes_.resize(this->alphabet_bit_num_);
    for (size_t i = 0; i < nodes_.size(); ++i)
    {
        nodes_[i] = new WaveletTreeNode(this->support_select_, this->dense_);
        nodes_[i]->load(istr);
    }
    for (size_t i = 1; i < nodes_.size(); ++i)
    {
        nodes_[i]->parent_ = nodes_[i - 1];
        nodes_[i - 1]->left_ = nodes_[i];
        nodes_[i - 1]->right_ = nodes_[i];
    }
}

}
}

NS_IZENELIB_AM_END

#endif
