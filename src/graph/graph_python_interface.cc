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

#include "graph_filtering.hh"
#include "graph.hh"
#include "graph_python_interface.hh"

#include <boost/python.hpp>
#include <boost/lambda/bind.hpp>
#include <set>

using namespace std;
using namespace boost;
using namespace graph_tool;

struct get_vertex_iterator
{
    template <class Graph>
    void operator()(const Graph* gp, const GraphInterface& gi,
                    python::object& iter) const
    {
        const Graph& g = *gp;
        typedef typename graph_traits<Graph>::vertex_iterator vertex_iterator;
        iter = python::object(PythonIterator<PythonVertex,
                                             vertex_iterator>(gi, vertices(g)));
    }
};

python::object
GraphInterface::Vertices() const
{
    python::object iter;
    run_action<>()(*this, lambda::bind<void>(get_vertex_iterator(), lambda::_1,
                                             lambda::var(*this),
                                             lambda::var(iter)))();
    return iter;
}

struct get_vertex_soft
{
    template <class Graph>
    void operator()(const Graph* gp, 
                    const GraphInterface& gi,
                    size_t i, python::object& v) const
    {
        const Graph& g = *gp;
        v = python::object(PythonVertex(gi, vertex(i, g)));
    }
};

struct get_vertex_hard
{
    template <class Graph>
    void operator()(const Graph* gp, const GraphInterface& gi, size_t i,
                    python::object& v) const
    {
        const Graph& g = *gp;
        size_t c = 0;
        typename graph_traits<Graph>::vertex_iterator vi, v_end;
        for (tie(vi, v_end) = vertices(g); vi != v_end; ++vi)
        {
            if (c == i)
                v = python::object(PythonVertex(gi, *vi));
            ++c;
        }
    }
};

python::object
GraphInterface::Vertex(size_t i) const
{
    python::object v;
    if (IsVertexFilterActive())
        run_action<>()(*this, lambda::bind<void>(get_vertex_hard(), lambda::_1, 
                                                 lambda::var(*this),
                                                 i, lambda::var(v)))();
    else
        run_action<detail::never_filtered>()
            (*this, lambda::bind<void>(get_vertex_soft(), lambda::_1,
                                       lambda::var(*this), i, 
                                       lambda::var(v)))();        
    return v;
}

struct get_edge_iterator
{
    template <class Graph>
    void operator()(const Graph* gp,  const GraphInterface& gi,
                    python::object& iter) const
    {
        const Graph& g = *gp;
        typedef typename graph_traits<Graph>::edge_iterator edge_iterator;
        iter = python::object(PythonIterator<PythonEdge<Graph>,
                                             edge_iterator>(gi, edges(g)));
    }
};

python::object
GraphInterface::Edges() const
{
    python::object iter;
    run_action<>()(*this, lambda::bind<void>(get_edge_iterator(), lambda::_1,
                                             lambda::var(*this),
                                             lambda::var(iter)))();
    return iter;
}

size_t GraphInterface::GetEdgeHash(const edge_t& e) const
{
    return hash<size_t>()(_edge_index[e]);
}

python::object GraphInterface::AddVertex()
{
    return python::object(PythonVertex(*this, add_vertex(_mg)));
}

struct shift_vertex_property
{
    template <class Graph, class PropertyMap>
    void operator()(const Graph* gp, size_t vi, PropertyMap pmap)
        const
    {
        const Graph& g = *gp;
        size_t N = num_vertices(g);
        for (size_t i = vi; i < N-1; ++i)
        {
            pmap[vertex(i, g)] = pmap[vertex(i+1, g)];
        }
    }
};

void GraphInterface::RemoveVertex(python::object v)
{
    PythonVertex& pv = python::extract<PythonVertex&>(v);
    vertex_t dv = pv.GetDescriptor();
    
    //shift properties. O(N)
    for (typeof(_properties.begin()) p = _properties.begin();
         p != _properties.end(); ++p)
    {
        if (p->second->key() == typeid(vertex_t))
        {
            
            typedef property_map_types::apply<value_types,
                                              vertex_index_map_t,
                                              mpl::bool_<false> >::type
                properties;

            run_action<>()(*this, 
                           lambda::bind<void>(shift_vertex_property(), 
                                              lambda::_1, _vertex_index[dv], 
                                              lambda::_2), properties())
                (prop(p->first, _vertex_index, _properties));
        }
    }

    //remove vertex
    clear_vertex(dv, _mg);
    remove_vertex(dv, _mg);
}

struct add_new_edge
{
    template <class Graph>
    void operator()(Graph* gp, const GraphInterface& gi, const PythonVertex& s,
                    const PythonVertex& t, python::object& new_e) const
    {
        Graph& g = *gp;
        typename graph_traits<Graph>::edge_descriptor e = 
            add_edge(s.GetDescriptor(), t.GetDescriptor(), g).first;
        new_e = python::object(PythonEdge<Graph>(gi, e));
    }
};

python::object GraphInterface::AddEdge(python::object s, python::object t)
{
    PythonVertex& src = python::extract<PythonVertex&>(s);
    PythonVertex& tgt = python::extract<PythonVertex&>(t);
    python::object new_e;
    run_action<>()(*this, lambda::bind<void>(add_new_edge(), lambda::_1, 
                                             lambda::var(*this), src, tgt, 
                                             lambda::var(new_e)))();
    return new_e;
}

struct get_edge_descriptor
{
    template <class Graph>
    void operator()(const Graph* gp, python::object& v,
                    typename GraphInterface::edge_t& edge)
        const
    {
        PythonEdge<Graph>& pv = python::extract<PythonEdge<Graph>&>(v);
        edge = pv.GetDescriptor();
    }
};

void GraphInterface::RemoveEdge(python::object e)
{
    graph_traits<multigraph_t>::edge_descriptor de;
    run_action<>()(*this, 
                   lambda::bind<void>(get_edge_descriptor(), lambda::_1,
                                      lambda::var(e), lambda::var(de)))();
    remove_edge(de, _mg);
}

struct get_property_map
{
    get_property_map(const string& name, dynamic_property_map& dp,
                     python::object& pmap)
        : _name(name), _dp(dp), _pmap(pmap) {}

    template <class ValueType>
    void operator()(ValueType) const
    {
        if (typeid(ValueType) == _dp.value())
            _pmap = python::object(PythonPropertyMap<ValueType>(_name, _dp));
    }

    const string& _name;
    dynamic_property_map& _dp;
    python::object& _pmap;
};

void GraphInterface::Clear()
{
    _mg.clear();
    _properties = dynamic_properties();
}

python::dict
GraphInterface::GetVertexProperties() const
{
    typedef graph_traits<multigraph_t>::vertex_descriptor vertex_t;;

    python::dict props;
    for(typeof(_properties.begin()) iter = _properties.begin();
        iter != _properties.end(); ++iter)
        if (iter->second->key() == typeid(vertex_t))
        {
            python::object pmap;
            mpl::for_each<value_types>(get_property_map
                                       (iter->first, *iter->second, pmap));
            props[iter->first] = pmap;
        }
    return props;
}

python::dict
GraphInterface::GetEdgeProperties() const
{
    typedef graph_traits<multigraph_t>::edge_descriptor edge_t;;

    python::dict props;
    for(typeof(_properties.begin()) iter = _properties.begin();
        iter != _properties.end(); ++iter)
        if (iter->second->key() == typeid(edge_t))
        {
            python::object pmap;
            mpl::for_each<value_types>(get_property_map
                                       (iter->first, *iter->second, pmap));
            props[iter->first] = pmap;
        }
    return props;
}

python::dict
GraphInterface::GetGraphProperties() const
{
    python::dict props;
    for(typeof(_properties.begin()) iter = _properties.begin();
        iter != _properties.end(); ++iter)
        if (iter->second->key() == typeid(graph_property_tag))
        {
            python::object pmap;
            mpl::for_each<value_types>(get_property_map
                                       (iter->first, *iter->second, pmap));
            props[iter->first] = pmap;
        }
    return props;
}

//
// Below are the functions with will properly register all the types to python,
// for every filter, type, etc.
//

// this will register all the Vertex/Edge classes to python
struct export_python_interface
{
    template <class Graph>
        void operator()(const Graph*, set<string>& v_iterators) const
    {
        using namespace boost::python;

        class_<PythonEdge<Graph> > ("Edge", no_init)
            .def("source", &PythonEdge<Graph>::GetSource)
            .def("target", &PythonEdge<Graph>::GetTarget)
            .def(python::self == python::self)
            .def(python::self != python::self)
            .def("__str__", &PythonEdge<Graph>::GetString)
            .def("__hash__", &PythonEdge<Graph>::GetHash);

        typedef typename graph_traits<Graph>::vertex_iterator vertex_iterator;
        if (v_iterators.find(typeid(vertex_iterator).name()) == 
            v_iterators.end())
        {
            class_<PythonIterator<PythonVertex,
                                  vertex_iterator> >("VertexIterator", no_init)
                .def("__iter__", objects::identity_function())
                .def("next", &PythonIterator<PythonVertex,
                                             vertex_iterator>::Next);
            v_iterators.insert(typeid(vertex_iterator).name());
        }


        typedef typename graph_traits<Graph>::edge_iterator edge_iterator;
        class_<PythonIterator<PythonEdge<Graph>,
                              edge_iterator> >("EdgeIterator", no_init)
            .def("__iter__", objects::identity_function())
            .def("next", &PythonIterator<PythonEdge<Graph>,
                                         edge_iterator>::Next);

        typedef typename graph_traits<Graph>::out_edge_iterator
            out_edge_iterator;
        class_<PythonIterator<PythonEdge<Graph>,
                              out_edge_iterator> >("OutEdgeIterator", no_init)
            .def("__iter__", objects::identity_function())
            .def("next", &PythonIterator<PythonEdge<Graph>,
                                         out_edge_iterator>::Next);

        typedef typename graph_traits<Graph>::directed_category
            directed_category;
        typedef typename is_convertible<directed_category,
                                        boost::directed_tag>::type is_directed;
        if (is_directed::value)
        {
            typedef typename in_edge_iteratorS<Graph>::type in_edge_iterator;
            class_<PythonIterator<PythonEdge<Graph>,
                                  in_edge_iterator> >("InEdgeIterator", no_init)
                .def("__iter__", objects::identity_function())
                .def("next", &PythonIterator<PythonEdge<Graph>,
                                             in_edge_iterator>::Next);
        }
    }
};

struct export_vertex_iterators
{
};


void export_python_properties(const GraphInterface&);

// register everything

void GraphInterface::ExportPythonInterface() const
{
    python::class_<PythonVertex>("Vertex", python::no_init)
        .def("in_degree", &PythonVertex::GetInDegree)
        .def("out_degree", &PythonVertex::GetOutDegree)
        .def("out_edges", &PythonVertex::OutEdges)
        .def("in_edges", &PythonVertex::InEdges)
        .def(python::self == python::self)
        .def(python::self != python::self)
        .def("__str__", &PythonVertex::GetString)
        .def("__hash__", &PythonVertex::GetHash);

    set<string> v_iterators;
    typedef mpl::transform<graph_tool::detail::all_graph_views,
                           mpl::quote1<add_pointer> >::type graph_views;
    mpl::for_each<graph_views>(lambda::bind<void>(export_python_interface(),
                                                  lambda::_1,
                                                  lambda::var(v_iterators)));
}
