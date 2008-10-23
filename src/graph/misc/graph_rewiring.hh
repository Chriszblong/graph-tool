// graph-tool -- a general graph modification and manipulation thingy
//
// Copyright (C) 2007  Tiago de Paula Peixoto <tiago@forked.de>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef GRAPH_REWIRING_HH
#define GRAPH_REWIRING_HH

#include <tr1/unordered_set>
#include <boost/random.hpp>
#include <boost/functional/hash.hpp>

#include "graph.hh"
#include "graph_filtering.hh"

namespace graph_tool
{
using namespace std;
using namespace boost;

// this will get the source of an edge for directed graphs and the target for
// undirected graphs, i.e. "the source of an in-edge"
struct source_in
{
    template <class Graph>
    typename graph_traits<Graph>::vertex_descriptor
    operator()(typename graph_traits<Graph>::edge_descriptor e, const Graph& g)
    {
        return get_source(e, g, typename is_convertible
                          <typename graph_traits<Graph>::directed_category,
                           directed_tag>::type());
    }

    template <class Graph>
    typename graph_traits<Graph>::vertex_descriptor
    get_source(typename graph_traits<Graph>::edge_descriptor e, const Graph& g,
               true_type)
    {
        return source(e, g);
    }

    template <class Graph>
    typename graph_traits<Graph>::vertex_descriptor
    get_source(typename graph_traits<Graph>::edge_descriptor e, const Graph& g,
               false_type)
    {
        return target(e, g);
    }
};

// this will get the target of an edge for directed graphs and the source for
// undirected graphs, i.e. "the target of an in-edge"
struct target_in
{
    template <class Graph>
    typename graph_traits<Graph>::vertex_descriptor
    operator()(typename graph_traits<Graph>::edge_descriptor e, const Graph& g)
    {
        return get_target(e, g, typename is_convertible
                          <typename graph_traits<Graph>::directed_category,
                           directed_tag>::type());
    }

    template <class Graph>
    typename graph_traits<Graph>::vertex_descriptor
    get_target(typename graph_traits<Graph>::edge_descriptor e, const Graph& g,
               true_type)
    {
        return target(e, g);
    }

    template <class Graph>
    typename graph_traits<Graph>::vertex_descriptor
    get_target(typename graph_traits<Graph>::edge_descriptor e, const Graph& g,
               false_type)
    {
        return source(e, g);
    }
};

// returns true if vertices u and v are adjacent. This is O(k(u)).
template <class Graph>
bool is_adjacent(typename graph_traits<Graph>::vertex_descriptor u,
                 typename graph_traits<Graph>::vertex_descriptor v,
                 const Graph& g )
{
    typename graph_traits<Graph>::out_edge_iterator e, e_end;
    for (tie(e, e_end) = out_edges(u, g); e != e_end; ++e)
    {
        if (target(*e,g) == v)
            return true;
    }
    return false;
}

// this functor will swap the source of the edge e with the source of edge se
// and the target of edge e with the target of te
struct swap_edge_triad
{
    template <class Graph, class NewEdgeMap>
    static bool parallel_check(typename graph_traits<Graph>::edge_descriptor e,
                               typename graph_traits<Graph>::edge_descriptor se,
                               typename graph_traits<Graph>::edge_descriptor te,
                               NewEdgeMap edge_is_new, const Graph &g)
    {
        // We want to check that if we swap the source of 'e' with the source of
        // 'se', and the target of 'te' with the target of 'e', as such
        //
        //  (s)    -e--> (t)          (ns)   -e--> (nt)
        //  (ns)   -se-> (se_t)   =>  (s)    -se-> (se_t)
        //  (te_s) -te-> (nt)         (te_s) -te-> (t),
        //
        // no parallel edges are introduced. We must considered only "new
        // edges", i.e., edges which were already sampled and swapped. "Old
        // edges" will have their chance of being swapped, and then they'll be
        // checked for parallelism.

        typename graph_traits<Graph>::vertex_descriptor
            s = source(e, g),          // current source
            t = target(e, g),          // current target
            ns = source(se, g),        // new source
            nt = target_in()(te, g),   // new target
            te_s = source_in()(te, g), // target edge source
            se_t = target(se, g);      // source edge target


        if (edge_is_new[se] && (ns == s) && (nt == se_t))
            return true; // e is parallel to se after swap
        if (edge_is_new[te] && (te_s == ns) && (nt == t) )
            return true; // e is parallel to te after swap
        if (edge_is_new[te] && edge_is_new[se] && (te != se) &&
             (s == te_s) && (t == se_t))
            return true; // se is parallel to te after swap
        if (is_adjacent_in_new(ns,  nt, se, te, edge_is_new, g))
            return true; // e would clash with an existing (new) edge
        if (is_adjacent_in_new(te_s, t, se, te, edge_is_new, g))
            return true; // te would clash with an existing (new) edge
        if (is_adjacent_in_new(s, se_t, se, te, edge_is_new, g))
            return true; // se would clash with an existing (new) edge
        return false; // the coast is clear - hooray!
    }

    // returns true if vertices u and v are adjacent in the new graph. This is
    // O(k(u)).
    template <class Graph, class EdgeIsNew>
    static bool is_adjacent_in_new
        (typename graph_traits<Graph>::vertex_descriptor u,
         typename graph_traits<Graph>::vertex_descriptor v,
         typename graph_traits<Graph>::edge_descriptor e1,
         typename graph_traits<Graph>::edge_descriptor e2,
         EdgeIsNew edge_is_new, const Graph& g)
    {
        typename graph_traits<Graph>::out_edge_iterator e, e_end;
        for (tie(e, e_end) = out_edges(u, g); e != e_end; ++e)
        {
            if (target(*e,g) == v && edge_is_new[*e] &&
                (*e != e1) && (*e != e2))
                return true;
        }
        return false;
    }

    template <class Graph, class EdgeIndexMap, class EdgesType>
    void operator()(typename graph_traits<Graph>::edge_descriptor e,
                    typename graph_traits<Graph>::edge_descriptor se,
                    typename graph_traits<Graph>::edge_descriptor te,
                    EdgesType& edges, EdgeIndexMap edge_index, Graph& g)
    {
        // swap the source of the edge 'e' with the source of edge 'se' and the
        // target of edge 'e' with the target of 'te', as such:
        //
        //  (s)    -e--> (t)          (ns)   -e--> (nt)
        //  (ns)   -se-> (se_t)   =>  (s)    -se-> (se_t)
        //  (te_s) -te-> (nt)         (te_s) -te-> (t),

        // new edges which will replace the old ones
        typename graph_traits<Graph>::edge_descriptor ne, nse, nte;

        // split cases where different combinations of the three edges are
        // the same
        if(se != te)
        {
            ne = add_edge(source(se, g), target_in()(te, g), g).first;
            if(e != se)
            {
                nse = add_edge(source(e, g), target(se, g), g).first;
                edge_index[nse] = edge_index[se];
                remove_edge(se, g);
                edges[edge_index[nse]] = nse;
            }
            if(e != te)
            {
                nte = add_edge(source_in()(te, g), target(e, g), g).first;
                edge_index[nte] = edge_index[te];
                remove_edge(te, g);
                edges[edge_index[nte]] = nte;
            }
            edge_index[ne] = edge_index[e];
            remove_edge(e, g);
            edges[edge_index[ne]] = ne;
        }
        else
        {
            if(e != se)
            {
                // se and te are the same. swapping indexes only.
                swap(edge_index[se], edge_index[e]);
                edges[edge_index[se]] = se;
                edges[edge_index[e]] = e;
            }
        }
    }
};

// main rewire loop
template <template <class Graph, class EdgeIndexMap> class RewireStrategy>
struct graph_rewire
{
    template <class Graph, class EdgeIndexMap>
    void operator()(Graph* gp, EdgeIndexMap edge_index, rng_t& rng,
                    bool self_loops, bool parallel_edges) const
    {
        typedef typename graph_traits<Graph>::vertex_descriptor vertex_t;
        typedef typename graph_traits<Graph>::edge_descriptor edge_t;

        Graph& g = *gp;

        if (!self_loops)
        {
            // check the existence of self-loops
            bool has_self_loops = false;
            int i, N = num_vertices(g);
            #pragma omp parallel for default(shared) private(i) \
                schedule(dynamic)
            for (i = 0; i < N; ++i)
            {
                vertex_t v = vertex(i, g);
                if (v == graph_traits<Graph>::null_vertex())
                    continue;
                if (is_adjacent(v, v, g))
                    has_self_loops = true;
            }
            if (has_self_loops)
                throw GraphException("Self-loop detected. Can't rewire graph "
                                     "without self-loops if it already contains"
                                     " self-loops!");
        }

        if (!parallel_edges)
        {
            // check the existence of parallel edges
            bool has_parallel_edges = false;
            int i, N = num_vertices(g);
            #pragma omp parallel for default(shared) private(i) \
                schedule(dynamic)
            for (i = 0; i < N; ++i)
            {
                vertex_t v = vertex(i, g);
                if (v == graph_traits<Graph>::null_vertex())
                    continue;

                tr1::unordered_set<vertex_t> targets;
                typename graph_traits<Graph>::out_edge_iterator e, e_end;
                for (tie(e, e_end) = out_edges(v, g); e != e_end; ++e)
                {
                    if (targets.find(target(*e, g)) != targets.end())
                        has_parallel_edges = true;
                    else
                        targets.insert(target(*e, g));
                }
            }

            if (has_parallel_edges)
                throw GraphException("Parallel edge detected. Can't rewire "
                                     "graph without parallel edges if it "
                                     "already contains parallel edges!");
        }

        RewireStrategy<Graph, EdgeIndexMap> rewire(g, edge_index, rng);

        vector<edge_t> edges(num_edges(g));
        int i, N = num_vertices(g);
        #pragma omp parallel for default(shared) private(i) schedule(dynamic)
        for (i = 0; i < N; ++i)
        {
            vertex_t v = vertex(i, g);
            if (v == graph_traits<Graph>::null_vertex())
                continue;
            typename graph_traits<Graph>::out_edge_iterator e, e_end;
            for (tie(e, e_end) = out_edges(v, g); e != e_end; ++e)
                edges[edge_index[*e]] = *e;
        }

        // for each edge simultaneously rewire its source and target
        for (i = 0; i < int(edges.size()); ++i)
        {
            typename graph_traits<Graph>::edge_descriptor e = edges[i];
            typename graph_traits<Graph>::edge_descriptor se, te;
            tie(se, te) = rewire(e, edges, self_loops, parallel_edges);
            swap_edge_triad()(e, se, te, edges, edge_index, g);
        }
    }
};

// This will iterate over a random permutation of a random access sequence, by
// swapping the values of the sequence as it iterates
template <class RandomAccessIterator, class RNG>
class random_permutation_iterator
{
public:
    random_permutation_iterator(RandomAccessIterator first,
                                RandomAccessIterator last, RNG& rng )
        : _i(first), _last(last), _rng(rng)
    {
        std::iter_swap(_i, _i + _rng(_last - _i));
    }
    typename RandomAccessIterator::value_type operator*()
    {
        return *_i;
    }
    random_permutation_iterator& operator++()
    {
        ++_i;
        if(_i != _last)
            std::iter_swap(_i, _i + _rng(_last - _i));
        return *this;
    }
    bool operator==(const RandomAccessIterator& i)
    {
        return _i == i;
    }
    bool operator!=(const RandomAccessIterator& i)
    {
        return _i != i;
    }
private:
    RandomAccessIterator _i, _last;
    RNG& _rng;
};

// utility function for random_permutation_iterator
template <class RandomAccessIterator, class RNG>
inline random_permutation_iterator<RandomAccessIterator,RNG>
make_random_permutation_iterator(RandomAccessIterator first,
                                 RandomAccessIterator last, RNG& rng)
{
    return random_permutation_iterator<RandomAccessIterator,RNG>(first, last,
                                                                 rng);
}

// this is the mother class for edge-based rewire strategies
// it contains the common loop for finding edges to swap, so different
// strategies need only to specify where to sample the edges from.
template<class Graph, class EdgeIndexMap>
class EdgeBasedRewireStrategy
{
public:
    typedef typename graph_traits<Graph>::edge_descriptor edge_t;
    typedef typename EdgeIndexMap::value_type index_t;

    EdgeBasedRewireStrategy (const Graph& g, EdgeIndexMap edge_index,
                              rng_t& rng)
        : _g(g), _edge_is_new(edge_index), random(rng) {}

    template<class EdgesType>
    pair<edge_t, edge_t> operator()(const edge_t& e, const EdgesType& edges,
                                    bool self_loops, bool parallel_edges)
    {
        // where should we sample the edges from
        vector<index_t> *edges_source, *edges_target;
        get_edges(e, &edges_source, &edges_target);

        //try randomly drawn pairs of edges until one satisfies all the
        //consistency checks
        bool found = false;
        edge_t es, et;
        typedef random_permutation_iterator
            <typename vector<index_t>::iterator, random_t> random_edge_iter;

        random_edge_iter esi(edges_source->begin(), edges_source->end(),
                             random);
        for (; esi != edges_source->end() && !found; ++esi)
        {
            es = edges[*esi];
            if(!self_loops) // reject self-loops if not allowed
            {
                if((source(e, _g) == target(es, _g)))
                    continue;
            }

            random_edge_iter eti(edges_target->begin(), edges_target->end(),
                                 random);
            for (; eti != edges_target->end() && !found; ++eti)
            {
                et = edges[*eti];
                if (!self_loops) // reject self-loops if not allowed
                {
                    if ((source(es, _g) == target_in()(et, _g)) ||
                        (source_in()(et, _g) == target(e, _g)))
                        continue;
                }
                if (!parallel_edges) // reject parallel edges if not allowed
                {
                    if (swap_edge_triad::parallel_check(e, es, et,
                                                        _edge_is_new, _g))
                        continue;
                }
                found = true;
            }
        }
        if (!found)
            throw GraphException("Couldn't find random pair of edges to swap"
                                 "... This is a bug.");
        _edge_is_new[e]=true;
        return make_pair(es, et);
    }

    virtual void get_edges(const edge_t& e, vector<index_t>** edges_source,
                   vector<index_t>** edges_target) = 0;

    virtual ~EdgeBasedRewireStrategy() {}

private:
    const Graph& _g;
    vector_property_map<bool, EdgeIndexMap> _edge_is_new;
    typedef random_number_generator<rng_t, size_t> random_t;
    random_t random;
};

// this will rewire the edges so that the combined (in, out) degree distribution
// will be the same, but all the rest is random
template <class Graph, class EdgeIndexMap>
class RandomRewireStrategy
    : public EdgeBasedRewireStrategy<Graph,EdgeIndexMap>
{
public:
    typedef typename graph_traits<Graph>::vertex_descriptor vertex_t;
    typedef typename graph_traits<Graph>::edge_descriptor edge_t;
    typedef typename EdgeIndexMap::value_type index_t;

    RandomRewireStrategy (const Graph& g, EdgeIndexMap edge_index,
                              rng_t& rng)
        : EdgeBasedRewireStrategy<Graph,EdgeIndexMap>(g, edge_index, rng), _g(g)
    {
        int i, N = num_vertices(_g);
        for (i = 0; i < N; ++i)
        {
            vertex_t v = vertex(i, _g);
            if (v == graph_traits<Graph>::null_vertex())
                continue;
            typename graph_traits<Graph>::out_edge_iterator e_i, e_i_end;
            for (tie(e_i, e_i_end) = out_edges(v, _g); e_i != e_i_end; ++e_i)
            {
                _all_edges.push_back(edge_index[*e_i]);
            }
        }
    }
    void get_edges(const edge_t& e, vector<index_t>** edges_source,
                   vector<index_t>** edges_target)
    {
        *edges_source = &_all_edges;
        *edges_target = &_all_edges;
    }
private:
    const Graph& _g;
    vector<index_t> _all_edges;
};


// this will rewire the edges so that the (in,out) degree distributions and the
// (in,out)->(in,out) correlations will be the same, but all the rest is random
template <class Graph, class EdgeIndexMap>
class CorrelatedRewireStrategy
    : public EdgeBasedRewireStrategy<Graph,EdgeIndexMap>
{
public:
    typedef typename graph_traits<Graph>::vertex_descriptor vertex_t;
    typedef typename graph_traits<Graph>::edge_descriptor edge_t;
    typedef typename EdgeIndexMap::value_type index_t;

    CorrelatedRewireStrategy (const Graph& g, EdgeIndexMap edge_index,
                              rng_t& rng)
        : EdgeBasedRewireStrategy<Graph,EdgeIndexMap>(g, edge_index, rng), _g(g)
    {
        int i, N = num_vertices(_g);
        for (i = 0; i < N; ++i)
        {
            vertex_t v = vertex(i, _g);
            if (v == graph_traits<Graph>::null_vertex())
                continue;
            typename graph_traits<Graph>::out_edge_iterator e_i, e_i_end;
            for (tie(e_i, e_i_end) = out_edges(v, _g); e_i != e_i_end; ++e_i)
            {
                _edges_source_by
                    [make_pair(in_degreeS()(source(*e_i, _g), _g),
                                  out_degree(source(*e_i, _g), _g))]
                    .push_back(edge_index[*e_i]);
                _edges_target_by
                    [make_pair(in_degreeS()(target_in()(*e_i, _g), _g),
                               out_degree(target_in()(*e_i, _g), _g))]
                    .push_back(edge_index[*e_i]);
            }
        }
    }
    void get_edges(const edge_t& e, vector<index_t>** edges_source,
                   vector<index_t>** edges_target)
    {
        *edges_source =
            &_edges_source_by[make_pair(in_degreeS()(source(e, _g), _g),
                                        out_degree(source(e, _g), _g))];
        *edges_target =
            &_edges_target_by[make_pair(in_degreeS()(target_in()(e, _g), _g),
                                        out_degree(target_in()(e, _g), _g))];
    }
private:
    const Graph& _g;
    typedef tr1::unordered_map<pair<size_t, size_t>, vector<index_t>,
                            hash<pair<size_t, size_t> > > edges_by_end_deg_t;
    edges_by_end_deg_t _edges_source_by, _edges_target_by;
};

} // graph_tool namespace

#endif // GRAPH_REWIRING_HH
