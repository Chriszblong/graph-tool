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

#ifndef GRAPH_HH
#define GRAPH_HH

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>

#include <boost/vector_property_map.hpp>
#include <boost/dynamic_property_map.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/lambda/bind.hpp>
#include <boost/variant.hpp>
#include <boost/python/object.hpp>
#include <boost/python/dict.hpp>
#include <boost/mpl/vector.hpp>
#include "graph_properties.hh"
#include "config.h"

namespace graph_tool
{
using namespace std;
using namespace boost;

// GraphInterface
// this class is the main interface to the internally kept graph. This is how
// the external world will manipulate the graph. All the algorithms should be
// registered here. This class will be exported to python in graph_bind.hh

namespace detail
{
// Generic graph_action functor. See graph_filtering.hh for details.
template <class Action, class GraphViews,
          class TR1=boost::mpl::vector<>, class TR2=boost::mpl::vector<>,
          class TR3=boost::mpl::vector<>, class TR4=boost::mpl::vector<> >
struct graph_action;
}

// default visibility is necessary for related typeinfo objects to work across
// DSO boundaries
#pragma GCC visibility push(default)
class GraphInterface
{
public:
    GraphInterface();
    GraphInterface(const GraphInterface& gi);
    ~GraphInterface();

    // useful enums

    typedef enum
    {
        IN_DEGREE,
        OUT_DEGREE,
        TOTAL_DEGREE
    } degree_t;

    // general "degree" type, i.e., either a degree_t above or a string
    // representing a scalar vertex property
    typedef boost::variant<degree_t, boost::any> deg_t;

    //
    // Basic manipulation
    //

    size_t GetNumberOfVertices() const;
    size_t GetNumberOfEdges() const;
    void SetDirected(bool directed) {_directed = directed;}
    bool GetDirected() {return _directed;}
    void SetReversed(bool reversed) {_reversed = reversed;}
    bool GetReversed() {return _reversed;}

    // graph filtering
    void SetVertexFilterProperty(boost::any prop, bool invert);
    bool IsVertexFilterActive() const;
    void SetEdgeFilterProperty(boost::any prop, bool invert);
    bool IsEdgeFilterActive() const;

    // graph modification
    void InsertPropertyMap(string name, boost::any map);
    void ReIndexEdges();
    void PurgeVertices(); // removes filtered vertices
    void PurgeEdges();    // removes filtered edges
    void Clear();
    void ClearEdges();
    void ShiftVertexProperty(boost::any map, size_t index) const;
    void CopyVertexProperty(const GraphInterface& src, boost::any prop_src,
                            boost::any prop_tgt);
    void CopyEdgeProperty(const GraphInterface& src, boost::any prop_src,
                          boost::any prop_tgt);

    //
    // python interface
    //
    python::object Vertices() const;
    python::object Vertex(size_t i) const;
    python::object Edges() const;

    python::object AddVertex();
    void           RemoveVertex(const python::object& v);
    python::object AddEdge(const python::object& s, const python::object& t);
    void           RemoveEdge(const python::object& e);

    // used for graph properties
    graph_property_tag GetDescriptor() const { return graph_property_tag(); }
    bool CheckValid() const {return true;}

    void ExportPythonInterface() const;

    // I/O
    void WriteToFile(string s, python::object pf, string format,
                     python::list properties);
    python::tuple ReadFromFile(string s, python::object pf, string format);

    //
    // Internal types
    //

    // the following defines the edges' internal properties
    typedef property<edge_index_t, size_t> EdgeProperty;

    // this is the main graph type
    typedef adjacency_list <vecS, // edges
                            vecS, // vertices
                            bidirectionalS,
                            no_property,
                            EdgeProperty>  multigraph_t;
    typedef graph_traits<multigraph_t>::vertex_descriptor vertex_t;
    typedef graph_traits<multigraph_t>::edge_descriptor edge_t;

    typedef property_map<multigraph_t,vertex_index_t>::type vertex_index_map_t;
    typedef property_map<multigraph_t,edge_index_t>::type edge_index_map_t;
    typedef ConstantPropertyMap<size_t,graph_property_tag> graph_index_map_t;

    // internal access

    multigraph_t& GetGraph() {return _mg;}
    vertex_index_map_t GetVertexIndex() {return _vertex_index;}
    edge_index_map_t   GetEdgeIndex()   {return _edge_index;}
    graph_index_map_t  GetGraphIndex()  {return graph_index_map_t(0);}

private:
    // Gets the encapsulated graph view. See graph_filtering.cc for details
    boost::any GetGraphView() const;

    // Generic graph_action functor. See graph_filtering.hh for details.
    template <class Action,
              class TR1=boost::mpl::vector<>, class TR2=boost::mpl::vector<>,
              class TR3=boost::mpl::vector<>, class TR4=boost::mpl::vector<> >
    friend struct detail::graph_action;

    // Arbitrary code execution
    template <class Action>
    friend void RunAction(GraphInterface& g, const Action& a);

    // python interface
    friend class PythonVertex;
    template <class Graph>
    friend class PythonEdge;

    // this is the main graph
    multigraph_t _mg;

    // this will hold an instance of the graph views at run time
    vector<boost::any> _graph_views;

    // reverse and directed states
    bool _reversed;
    bool _directed;

    // vertex index map
    vertex_index_map_t _vertex_index;

    // edge index map
    edge_index_map_t _edge_index;
    vector<size_t> _free_indexes; // indexes of deleted edges to be used up for
                                  // new edges to avoid needless fragmentation

    // graph index map
    graph_index_map_t _graph_index;

    // vertex filter
    typedef vector_property_map<uint8_t,vertex_index_map_t> vertex_filter_t;
    vertex_filter_t _vertex_filter_map;
    bool _vertex_filter_invert;
    bool _vertex_filter_active;

    // edge filter
    typedef vector_property_map<uint8_t,edge_index_map_t> edge_filter_t;
    edge_filter_t _edge_filter_map;
    bool _edge_filter_invert;
    bool _edge_filter_active;
};
#pragma GCC visibility pop

// Exceptions
// ==========
//
// This is the main exception which will be thrown the outside world, when
// things go wrong

#pragma GCC visibility push(default)
class GraphException : public std::exception
{
    string _error;
public:
    GraphException(const string& error) {_error = error;}
    virtual ~GraphException() throw () {}
    virtual const char * what () const throw () {return _error.c_str();}
protected:
    virtual void SetError(const string& error) {_error = error;}
};
#pragma GCC visibility pop

} //namespace graph_tool

#endif



