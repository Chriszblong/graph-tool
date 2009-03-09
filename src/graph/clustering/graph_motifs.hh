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

#ifndef GRAPH_MOTIFS_HH
#define GRAPH_MOTIFS_HH

#ifdef USING_OPENMP
#include <omp.h>
#endif

#include <boost/random.hpp>
#include <boost/functional/hash.hpp>
#include <boost/graph/copy.hpp>
#include <boost/graph/isomorphism.hpp>
#include <tr1/unordered_map>
#include <algorithm>

namespace graph_tool
{

typedef boost::mt19937 rng_t;

template <class Value>
void insert_sorted(vector<Value>& v, const Value& val)
{
    typeof(v.begin()) iter = lower_bound(v.begin(), v.end(), val);
    if (iter != v.end() && *iter == val)
        return; // no repetitions
    v.insert(iter, val);
}

template <class Value>
bool has_val(vector<Value>& v, const Value& val)
{
    typeof(v.begin()) iter = lower_bound(v.begin(), v.end(), val);
    if (iter == v.end())
        return false;
    return *iter == val;
}

template <class Graph, class Sampler>
void get_subgraphs(Graph g, typename graph_traits<Graph>::vertex_descriptor v,
                   size_t n,
                   vector<vector<typename graph_traits<Graph>::vertex_descriptor> >& subgraphs,
                   Sampler sampler)
{
    typedef typename graph_traits<Graph>::vertex_descriptor vertex_t;

    // extension and subgraph stack
    vector<vector<vertex_t> > ext_stack(1);
    vector<vector<vertex_t> > sub_stack(1);
    vector<vector<vertex_t> > sub_neighbours_stack(1);

    sub_stack[0].push_back(v);
    typename graph_traits<Graph>::out_edge_iterator e, e_end;
    for (tie(e, e_end) = out_edges(v, g); e != e_end; ++e)
    {
        typename graph_traits<Graph>::vertex_descriptor u = target(*e, g);
        if (u > v && !has_val(ext_stack[0], u))
        {
            insert_sorted(ext_stack[0], u);
            insert_sorted(sub_neighbours_stack[0],u);
        }
    }

    while (!sub_stack.empty())
    {
        vector<vertex_t>& ext = ext_stack.back();
        vector<vertex_t>& sub = sub_stack.back();
        vector<vertex_t>& sub_neighbours = sub_neighbours_stack.back();

        if (sub.size() == n)
        {
            // found a subgraph of the desired size; put it in the list and go
            // back a level
            subgraphs.push_back(sub);
            sub_stack.pop_back();
            ext_stack.pop_back();
            sub_neighbours_stack.pop_back();
            continue;
        }

        if (ext.empty())
        {
            // no where else to go
            ext_stack.pop_back();
            sub_stack.pop_back();
            sub_neighbours_stack.pop_back();
            continue;
        }
        else
        {
            // extend subgraph
            vector<vertex_t> new_ext, new_sub = sub,
                new_sub_neighbours = sub_neighbours;

            // remove w from ext
            vertex_t w = ext.back();
            ext.pop_back();

            // insert w in subgraph
            insert_sorted(new_sub, w);

            // update new_ext
            new_ext = ext;
            for (tie(e, e_end) = out_edges(w, g); e != e_end; ++e)
            {
                vertex_t u = target(*e,g);
                if (u > v)
                {
                    if (!has_val(sub_neighbours, u))
                        insert_sorted(new_ext, u);
                    insert_sorted(new_sub_neighbours, u);
                }
            }

            sampler(new_ext, ext_stack.size());

            ext_stack.push_back(new_ext);
            sub_stack.push_back(new_sub);
            sub_neighbours_stack.push_back(new_sub_neighbours);
        }
    }
}

struct sample_all
{
    template <class val_type>
    void operator()(vector<val_type>&, size_t) {}
};

struct sample_some
{
    sample_some(vector<double>& p, rng_t& rng): _p(&p), _rng(&rng) {}
    sample_some() {}

    template <class val_type>
    void operator()(vector<val_type>& extend, size_t d)
    {
        uniform_01<rng_t> random(*_rng);
        random_number_generator<rng_t> std_random(*_rng);
        double pd = (*_p)[d+1];
        size_t nc = extend.size();
        double u = nc*pd - floor(nc*pd);
        size_t n;
        double r;
        {
            #pragma omp critical
            r = random();
        }
        if (r < u)
            n = ceil(nc*pd);
        else
            n = floor(nc*pd);

        if (n == extend.size())
            return;
        if (n == 0)
        {
            extend.clear();
            return;
        }

        for (size_t i = 0; i < n; ++i)
        {
            size_t j;
            {
                #pragma omp critical
                j = i + std_random(extend.size()-i);
            }
            swap(extend[i], extend[j]);
        }
        extend.resize(n);
    }

    vector<double>* _p;
    rng_t* _rng;
};


template <class Graph, class GraphSG>
void make_subgraph
    (vector<typename graph_traits<Graph>::vertex_descriptor>& vlist,
     Graph& g, GraphSG& sub)
{
    for (size_t i = 0; i < vlist.size(); ++i)
        add_vertex(sub);
    for (size_t i = 0; i < vlist.size(); ++i)
    {
        typename graph_traits<Graph>::vertex_descriptor ov = vlist[i], ot;
        typename graph_traits<GraphSG>::vertex_descriptor nv = vertex(i,sub);
        typename graph_traits<Graph>::out_edge_iterator e, e_end;
        for (tie(e, e_end) = out_edges(ov, g); e != e_end; ++e)
        {
            ot = target(*e, g);
            typeof(vlist.begin()) viter =
                lower_bound(vlist.begin(), vlist.end(), ot);
            size_t ot_index = viter - vlist.begin();
            if (viter != vlist.end() && vlist[ot_index] == ot &&
                (is_directed::apply<Graph>::type::value || ot < ov))
                add_edge(nv, vertex(ot_index, sub), sub);
        }
    }
}

template <class Graph>
bool graph_cmp(Graph& g1, Graph& g2)
{
    if (num_vertices(g1) != num_vertices(g2) || num_edges(g1) != num_edges(g2))
        return false;

    typename graph_traits<Graph>::vertex_iterator v1, v1_end;
    typename graph_traits<Graph>::vertex_iterator v2, v2_end;
    tie(v2, v2_end) = vertices(g2);
    for (tie(v1, v1_end) = vertices(g1); v1 != v1_end; ++v1)
    {
        if (out_degree(*v1, g1) != out_degree(*v2, g2))
            return false;
        if (in_degreeS()(*v1, g1) != in_degreeS()(*v2, g2))
            return false;

        vector<typename graph_traits<Graph>::vertex_iterator> out1, out2;
        typename graph_traits<Graph>::out_edge_iterator e, e_end;
        for (tie(e, e_end) = out_edges(*v1, g1); e != e_end; ++e)
            out1.push_back(target(*e, g1));
        for (tie(e, e_end) = out_edges(*v2, g2); e != e_end; ++e)
            out2.push_back(target(*e, g2));
        sort(out1.begin(), out1.end());
        sort(out2.begin(), out2.end());
        if (out1 != out2)
            return false;
    }
    return true;
}

typedef adjacency_list<vecS,vecS,bidirectionalS> d_graph_t;
typedef adjacency_list<vecS,vecS,undirectedS> u_graph_t;

struct wrap_undirected
{
    template <class Graph>
    struct apply
    {
        typedef typename mpl::if_<typename is_directed::apply<Graph>::type,
                                  UndirectedAdaptor<Graph>,
                                  Graph&>::type type;
    };
};

struct get_all_motifs
{
    template <class Graph, class Sampler>
    void operator()(Graph* gp, size_t k, boost::any& list,
                    vector<size_t>& hist, Sampler sampler, double p,
                    bool comp_iso, bool fill_list, rng_t& rng)
        const
    {
        Graph& g = *gp;

        typedef typename mpl::if_<typename is_directed::apply<Graph>::type,
                                  d_graph_t,
                                  u_graph_t>::type graph_sg_t;

        vector<graph_sg_t>& subgraph_list =
            any_cast<vector<graph_sg_t>&>(list);

        tr1::unordered_map<size_t, vector<pair<size_t, graph_sg_t> > > sub_list;
        for (size_t i = 0; i < subgraph_list.size(); ++i)
            sub_list[num_edges(subgraph_list[i])].\
                push_back(make_pair(i,subgraph_list[i]));
        hist.resize(subgraph_list.size());

        vector<size_t> V;
        typename graph_traits<Graph>::vertex_iterator v, v_end;
        for (tie(v, v_end) = vertices(g); v != v_end; ++v)
            V.push_back(*v);
        if (p != 1)
        {
            uniform_01<rng_t> random(rng);
            random_number_generator<rng_t> std_random(rng);
            size_t n;

            if (random() < p)
                n = ceil(V.size()*p);
            else
                n = floor(V.size()*p);

            for (size_t i = 0; i < n; ++i)
            {
                size_t j = i + std_random(V.size()-i);
                swap(V[i], V[j]);
            }
            V.resize(n);
        }

        #ifdef USING_OPENMP
        omp_lock_t lock;
        omp_init_lock(&lock);
        #endif

        int i, N = V.size();
        // #pragma omp parallel for default(shared) private(i)       \
        //     schedule(dynamic)
        for (i = 0; i < N; ++i)
        {
            vector<vector<typename graph_traits<Graph>::vertex_descriptor> > subgraphs;
            typename graph_traits<Graph>::vertex_descriptor v = V[i];

            typename wrap_undirected::apply<Graph>::type ug(g);
            get_subgraphs(ug, v, k, subgraphs, sampler);

            for (size_t j = 0; j < subgraphs.size(); ++j)
            {
                graph_sg_t sub;
                make_subgraph(subgraphs[j], g, sub);

                #ifdef USING_OPENMP
                if (fill_list)
                    omp_set_lock(&lock);
                #endif

                bool found = false;
                for (size_t l = 0; l < sub_list[num_edges(sub)].size(); ++l)
                {
                    graph_sg_t& motif = sub_list[num_edges(sub)][l].second;
                    if (comp_iso && isomorphism(motif, sub))
                        found = true;
                    if (!comp_iso && graph_cmp(motif, sub))
                        found = true;
                    if (found)
                    {
                        #pragma omp critical
                        hist[sub_list[num_edges(sub)][l].first]++;
                        break;
                    }
                }

                if (found == false && fill_list)
                {
                    subgraph_list.push_back(sub);
                    sub_list[num_edges(sub)].
                        push_back(make_pair(subgraph_list.size()-1,sub));
                    hist.push_back(1);
                }

                #ifdef USING_OPENMP
                if (fill_list)
                    omp_unset_lock(&lock);
                #endif
            }
        }
        #ifdef USING_OPENMP
        omp_destroy_lock(&lock);
        #endif
    }
};

} //graph-tool namespace

#endif // GRAPH_MOTIFS_HH
