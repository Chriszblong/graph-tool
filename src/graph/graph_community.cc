// graph-tool -- a general graph modification and manipulation thingy
//
// Copyright (C) 2006  Tiago de Paula Peixoto <tiago@forked.de>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include <boost/lambda/lambda.hpp>
#include <boost/lambda/bind.hpp>
#include <boost/random.hpp>
#include <tr1/unordered_set>
#include <iomanip>

#include "graph.hh"
#include "histogram.hh"
#include "graph_filtering.hh"
#include "graph_selectors.hh"
#include "graph_properties.hh"

using namespace std;
using namespace boost;
using namespace boost::lambda;
using namespace graph_tool;

using std::tr1::unordered_map;
using std::tr1::unordered_set;

typedef boost::mt19937 rng_t;

template <class Graph>
size_t out_degree_no_loops(typename graph_traits<Graph>::vertex_descriptor v, const Graph &g)
{
    size_t k = 0;
    typename graph_traits<Graph>::adjacency_iterator a,a_end;
    for (tie(a,a_end) = adjacent_vertices(v,g); a != a_end; ++a)
	if (*a != v)
	    k++;
    return k;
}


//==============================================================================
// ManagedUnorderedMap
//==============================================================================
template <class Key, class Value>
class ManagedUnorderedMap: public tr1::unordered_map<Key,Value>
{
    typedef tr1::unordered_map<Key,Value> base_t;
public:
    void erase(typename base_t::iterator pos)
    {
	static_cast<base_t*>(this)->erase(pos);
	manage();
    }

    size_t erase(const Key& k)
    {
	size_t n = static_cast<base_t*>(this)->erase(k);
	manage();
	return n;
    }

    void manage()
    {
	if (this->bucket_count() > 2*this->size())
	{
	    base_t* new_map = new base_t;
	    for (typeof(this->begin()) iter = this->begin(); iter != this->end(); ++iter)
		(*new_map)[iter->first] = iter->second;
	    *static_cast<base_t*>(this) = *new_map;
	    delete new_map;
	}
    }

};


//==============================================================================
// GetCommunityStructure()
// computes the community structure through a spin glass system with 
// simulated annealing
//==============================================================================

template <template <class G, class CommunityMap> class NNKS>
struct get_communities
{
    template <class Graph, class WeightMap, class CommunityMap>
    void operator()(Graph& g, WeightMap weights, CommunityMap s, double gamma, size_t n_iter, pair<double,double> Tinterval, size_t Nspins, size_t seed, bool verbose) const
    {
	double Tmin = Tinterval.first;
	double Tmax = Tinterval.second;

	rng_t rng(static_cast<rng_t::result_type>(seed));
	boost::uniform_real<double> uniform_p(0.0,1.0);

	if (Nspins == 0)
	    Nspins = HardNumVertices()(g);

	ManagedUnorderedMap<size_t, size_t> Ns; // spin histogram
	ManagedUnorderedMap<size_t, map<double, unordered_set<size_t> > > global_term; // global energy term

	// init spins from [0,N-1] and global info
	uniform_int<size_t> sample_spin(0, Nspins-1);
	unordered_set<size_t> deg_set;
	typename graph_traits<Graph>::vertex_iterator v,v_end;
	for (tie(v,v_end) = vertices(g); v != v_end; ++v)
	{
	    s[*v] = sample_spin(rng);
	    Ns[s[*v]]++;
	    deg_set.insert(out_degree_no_loops(*v, g));
	}

	NNKS<Graph,CommunityMap> Nnnks(g, s); // this will retrieve the expected number of neighbours with given spin, in funcion of degree

	// setup global energy terms for all degrees and spins
	vector<size_t> degs;
	for (typeof(deg_set.begin()) iter = deg_set.begin(); iter != deg_set.end(); ++iter)
	    degs.push_back(*iter);
	for (size_t i = 0; i < degs.size(); ++i)
	    for (size_t sp = 0; sp < Nspins; ++sp)
		global_term[degs[i]][gamma*Nnnks(degs[i],sp)].insert(sp);


	// define cooling rate so that temperature starts at Tmax at temp_count == 0
	// and reaches Tmin at temp_count == n_iter - 1
	if (Tmin < numeric_limits<double>::epsilon())
	    Tmin = numeric_limits<double>::epsilon();
	double cooling_rate = -(log(Tmin)-log(Tmax))/(n_iter-1);

	// start the annealing
	for (size_t temp_count = 0; temp_count < n_iter; ++temp_count)
	{
	    double T = Tmax*exp(-cooling_rate*temp_count);
	    
	    bool steepest_descent = false; // flags if temperature is too low

	    // calculate the cumulative probabilities of each spin energy level
	    unordered_map<size_t, map<long double, pair<double, bool> > > cumm_prob;
	    unordered_map<size_t, unordered_map<double, long double> > energy_to_prob;
	    for (size_t i = 0; i < degs.size(); ++i)
	    {
		long double prob = 0;
		for (typeof(global_term[degs[i]].begin()) iter = global_term[degs[i]].begin(); iter != global_term[degs[i]].end(); ++iter)
		{
		    long double M = log(numeric_limits<long double>::max()/(Nspins*10));
		    long double this_prob = exp((long double)(-iter->first - degs[i])/T + M)*iter->second.size();
		    if (prob + this_prob != prob)
		    {
			prob += this_prob;
			cumm_prob[degs[i]][prob] = make_pair(iter->first, true);
			energy_to_prob[degs[i]][iter->first] = prob;
		    }
		    else
		    {
			energy_to_prob[degs[i]][iter->first] = 0;
		    }
		}
		if (prob == 0.0)
		    steepest_descent = true;
	    }

	    // list of spins which were updated
	    vector<pair<typename graph_traits<Graph>::vertex_descriptor,size_t> > spin_update;
	    spin_update.reserve(HardNumVertices()(g));

            // sample a new spin for every vertex
	    for (tie(v,v_end) = vertices(g); v != v_end; ++v)
	    {
		unordered_map<size_t, double> ns; // number of neighbours with spin 's' (weighted)
		
		// neighborhood spins info
		typename graph_traits<Graph>::out_edge_iterator e,e_end;
		for (tie(e,e_end) = out_edges(*v,g); e != e_end; ++e)
		{
		    typename graph_traits<Graph>::vertex_descriptor t = target(*e,g);
		    if (t != *v)
			ns[s[t]] += get(weights, *e);
		}
		    
		size_t k = out_degree_no_loops(*v,g);

		map<double, unordered_set<size_t> >& global_term_k = global_term[k];
		map<long double, pair<double, bool> >& cumm_prob_k = cumm_prob[k];

		// update energy levels with local info
		unordered_set<double> modified_energies;
		for (typeof(ns.begin()) iter = ns.begin(); iter != ns.end(); ++iter)
		{
		    double old_E = gamma*Nnnks(k, iter->first);
		    double new_E = old_E - ns[iter->first];
		    global_term_k[old_E].erase(iter->first);
		    if (global_term_k[old_E].empty())
			global_term_k.erase(old_E);
		    global_term_k[new_E].insert(iter->first);
		    modified_energies.insert(old_E);
		    modified_energies.insert(new_E);
		}
		
		// update probabilities
		size_t prob_mod_count = 0;
		for (typeof(modified_energies.begin()) iter = modified_energies.begin(); iter != modified_energies.end(); ++iter)
		{
		    if (energy_to_prob[k].find(*iter) != energy_to_prob[k].end())
			if (energy_to_prob[k][*iter] != 0.0)
			    cumm_prob_k[energy_to_prob[k][*iter]].second = false;
		    if (global_term_k.find(*iter) != global_term_k.end())
		    {
			long double M = log(numeric_limits<long double>::max()/(Nspins*10));
			long double prob = exp((long double)(-*iter - k)/T + M)*global_term_k[*iter].size();
			if (cumm_prob_k.empty() || cumm_prob_k.rbegin()->first + prob != cumm_prob_k.rbegin()->first)
			{
			    if (!cumm_prob_k.empty())
				prob += cumm_prob_k.rbegin()->first;
			    cumm_prob_k.insert(cumm_prob_k.end(), make_pair(prob, make_pair(*iter, true)));
			    prob_mod_count++;
			}
		    }
		}

		// choose the new energy
		double E = numeric_limits<double>::max();
		if (prob_mod_count == 0 && !modified_energies.empty() || steepest_descent)
		{
		    // Temperature too low! The computer precision is not enough to calculate the probabilities correctly.
		    // Switch to steepest descent mode....
		    steepest_descent = true;
		    E = global_term_k.begin()->first;
		}
		else
		{
		    // sample energy according to its probability
		    uniform_real<long double> prob_sample(0.0, max(cumm_prob_k.rbegin()->first, numeric_limits<long double>::epsilon()));
		    bool accept = false;
		    while (!accept)
		    {
			typeof(cumm_prob_k.begin()) upper = cumm_prob_k.upper_bound(prob_sample(rng));
			if (upper == cumm_prob_k.end())
			    upper--;
			E = upper->second.first;
			accept = upper->second.second;
		    }
		}

		//new spin (randomly chosen amongst those with equal energy)
		uniform_int<size_t> sample_spin(0,global_term_k[E].size()-1);
		typeof(global_term_k[E].begin()) iter = global_term_k[E].begin();
		advance(iter, sample_spin(rng));
		size_t a = *iter;

		// cleanup modified probabilities
		for (typeof(modified_energies.begin()) iter = modified_energies.begin(); iter != modified_energies.end(); ++iter)
		{
		    if (energy_to_prob[k].find(*iter) != energy_to_prob[k].end())
			if (energy_to_prob[k][*iter] != 0.0)			
			    cumm_prob_k[energy_to_prob[k][*iter]].second = true;
		    if (prob_mod_count > 0)
		    {
			cumm_prob_k.erase(cumm_prob_k.rbegin()->first);
			prob_mod_count--;
		    }
		}
		
		// cleanup modified energy levels
		for (typeof(ns.begin()) iter = ns.begin(); iter != ns.end(); ++iter)
		{
		    double new_E = gamma*Nnnks(k, iter->first);
		    double old_E = new_E - ns[iter->first];

		    global_term_k[old_E].erase(iter->first);
		    if (global_term_k[old_E].empty())
			global_term_k.erase(old_E);
		    global_term_k[new_E].insert(iter->first);
		}

		//update global info
		if (s[*v] != a)
		    spin_update.push_back(make_pair(*v, a));
	    }
	    
	    // flip spins and update Nnnks
	    for (size_t u = 0; u < spin_update.size(); ++u)
	    {
		typename graph_traits<Graph>::vertex_descriptor v = spin_update[u].first;
		size_t k = out_degree_no_loops(v, g);
		size_t a = spin_update[u].second;
		for (size_t i = 0; i < degs.size(); ++i)
		{
		    size_t nk = degs[i];
		    double old_E = gamma*Nnnks(nk,s[v]);
		    global_term[nk][old_E].erase(s[v]);
		    if (global_term[nk][old_E].empty())
			global_term[nk].erase(old_E);
		    old_E = gamma*Nnnks(nk,a);
		    global_term[nk][old_E].erase(a);
		    if (global_term[nk][old_E].empty())
			global_term[nk].erase(old_E);
		}
		    
		Nnnks.Update(k,s[v],a);
		Ns[s[v]]--;
		if (Ns[s[v]] == 0)
		    Ns.erase(s[v]);
		Ns[a]++;
		
		for (size_t i = 0; i < degs.size(); ++i)
		{
		    size_t nk = degs[i];
		    global_term[nk][gamma*Nnnks(nk,s[v])].insert(s[v]);
		    global_term[nk][gamma*Nnnks(nk,a)].insert(a);
		}
   		    
		// update spin
		s[v] = a;
	    }

	    if (verbose)
	    {
		static stringstream str;
		for (size_t j = 0; j < str.str().length(); ++j)
		    cout << "\b";
		str.str("");
		str << setw(lexical_cast<string>(n_iter).size()) << temp_count << " of " << n_iter 
		    << " (" << setw(2) << (temp_count+1)*100/n_iter << "%) " << "temperature: " << setw(14) << setprecision(10) << T << " spins: " << Ns.size() << " energy levels: ";
		size_t n_energy = 0;
		for (typeof(global_term.begin()) iter = global_term.begin(); iter != global_term.end(); ++iter)
		    n_energy += iter->second.size();
		str << setw(lexical_cast<string>(Nspins*degs.size()).size()) << n_energy;
		if (steepest_descent)
		    str << " (steepest descent)";
		cout << str.str() << flush;
	    }
	}
	if (verbose)
	    cout << endl;
    }
};

template <class Graph, class CommunityMap>
class NNKSErdosReyni
{
public:
    NNKSErdosReyni(Graph &g, CommunityMap s)
    {
	size_t N = 0;
	double _avg_k = 0.0;
	typename graph_traits<Graph>::vertex_iterator v,v_end;
	for (tie(v,v_end) = vertices(g); v != v_end; ++v)
	{
	    size_t k = out_degree_no_loops(*v,g); 
	    _avg_k += k;
	    N++;
	    _Ns[s[*v]]++;
	}
	_p = _avg_k/(N*N);
    }

    void Update(size_t k, size_t old_s, size_t s)
    {
	_Ns[old_s]--;
	if (_Ns[old_s] == 0)
	    _Ns.erase(old_s);
	_Ns[s]++;
    }

    double operator()(size_t k, size_t s) const 
    {
	size_t ns = 0;
	typeof(_Ns.begin()) iter = _Ns.find(s);
	if (iter != _Ns.end())
	    ns = iter->second;
	return _p*ns;
    }

private:
    double _p;
    ManagedUnorderedMap<size_t,size_t> _Ns;
};

template <class Graph, class CommunityMap>
class NNKSUncorr
{
public:
    NNKSUncorr(Graph &g, CommunityMap s): _g(g), _K(0)
    {
	typename graph_traits<Graph>::vertex_iterator v,v_end;
	for (tie(v,v_end) = vertices(_g); v != v_end; ++v)
	{
	    size_t k = out_degree_no_loops(*v, _g);
	    _K += k;
	    _Ks[s[*v]] += k; 
	}
    }

    void Update(size_t k, size_t old_s, size_t s)
    {
	_Ks[old_s] -= k;
	if (_Ks[old_s] == 0)
	    _Ks.erase(old_s);
	_Ks[s] += k; 
    }

    double operator()(size_t k, size_t s) const 
    {
	size_t ks = 0;
	typeof(_Ks.begin()) iter = _Ks.find(s);
	if (iter != _Ks.end())
	    ks = iter->second;
	return k*ks/double(_K);
    }

private:
    Graph& _g;
    size_t _K;
    ManagedUnorderedMap<size_t, size_t> _Ks;
};

template <class Graph, class CommunityMap>
class NNKSCorr
{
public:
    NNKSCorr(Graph &g, CommunityMap s): _g(g)
    {
	unordered_set<size_t> spins;

	typename graph_traits<Graph>::vertex_iterator v,v_end;
	for (tie(v,v_end) = vertices(_g); v != v_end; ++v)
	{
	    size_t k = out_degree_no_loops(*v, _g);
	    _Nk[k]++;
	    _Nks[k][s[*v]]++;
	    spins.insert(s[*v]);
	}
	
	size_t E = 0;
	typename graph_traits<Graph>::edge_iterator e,e_end;
	for (tie(e,e_end) = edges(_g); e != e_end; ++e)
	{
	    typename graph_traits<Graph>::vertex_descriptor s, t;
	    s = source(*e,g);
	    t = target(*e,g);
	    if (s != t)
	    {
		size_t k1 = out_degree_no_loops(s, g);
		size_t k2 = out_degree_no_loops(t, g);
		_Pkk[k1][k2]++;
		_Pkk[k2][k1]++;
		E++;
	    }
	}

	for (typeof(_Pkk.begin()) iter1 = _Pkk.begin(); iter1 != _Pkk.end(); ++iter1)
	{
	    double sum = 0;
	    for (typeof(iter1->second.begin()) iter2 = iter1->second.begin(); iter2 != iter1->second.end(); ++iter2)
		sum += iter2->second;
	    for (typeof(iter1->second.begin()) iter2 = iter1->second.begin(); iter2 != iter1->second.end(); ++iter2)
		iter2->second /= sum;
	}

	for (typeof(_Nk.begin()) k_iter = _Nk.begin(); k_iter != _Nk.end(); ++k_iter) 
	{
	    size_t k1 = k_iter->first;
	    _degs.push_back(k1);
	    for (typeof(spins.begin()) s_iter = spins.begin(); s_iter != spins.end(); ++s_iter)
		for (typeof(_Nk.begin()) k_iter2 = _Nk.begin(); k_iter2 != _Nk.end(); ++k_iter2) 
		{
		    size_t k2 = k_iter2->first;
		    if (_Nks[k2].find(*s_iter) != _Nks[k2].end())
			_NNks[k1][*s_iter] +=  k1*_Pkk[k1][k2] * _Nks[k2][*s_iter]/double(_Nk[k2]);
		}
	}
    }

    void Update(size_t k, size_t old_s, size_t s)
    {

	for (size_t i = 0; i < _degs.size(); ++i) 
	{
	    size_t k1 = _degs[i], k2 = k;
	    if (_Pkk.find(k1) == _Pkk.end())
		continue;
	    if (_Pkk.find(k1)->second.find(k2) == _Pkk.find(k1)->second.end())
		continue;
	    _NNks[k1][old_s] -=  k1*_Pkk[k1][k2] * _Nks[k2][old_s]/double(_Nk[k2]);
	    if (_NNks[k1][old_s] == 0.0)
		_NNks[k1].erase(old_s);
	    if (_Nks[k2].find(s) != _Nks[k2].end())
                _NNks[k1][s] -=  k1*_Pkk[k1][k2] * _Nks[k2][s]/double(_Nk[k2]);
	    if (_NNks[k1][s] == 0.0)
                _NNks[k1].erase(s);
	}

	_Nks[k][old_s]--;
	if (_Nks[k][old_s] == 0)
	    _Nks[k].erase(old_s);
	_Nks[k][s]++;

	for (size_t i = 0; i < _degs.size(); ++i) 
	{
	    size_t k1 = _degs[i], k2 = k;
	    if (_Pkk.find(k1) == _Pkk.end())
		continue;
	    if (_Pkk.find(k1)->second.find(k2) == _Pkk.find(k1)->second.end())
		continue;
	    _NNks[k1][old_s] +=  k1*_Pkk[k1][k2] * _Nks[k2][old_s]/double(_Nk[k2]);
	    if (_NNks[k1][old_s] == 0.0)
		_NNks[k1].erase(old_s);
	    _NNks[k1][s] +=  k1*_Pkk[k1][k2] * _Nks[k2][s]/double(_Nk[k2]);
	}

    }

    double operator()(size_t k, size_t s) const
    {
	const typeof(_NNks[k])& nnks = _NNks.find(k)->second;
	const typeof(nnks.begin()) iter = nnks.find(s);
	if (iter != nnks.end())
	    return iter->second;
	return 0.0;
    }

private:
    Graph& _g;
    vector<size_t> _degs;
    unordered_map<size_t, size_t> _Nk;
    unordered_map<size_t, unordered_map<size_t,double> > _Pkk;
    unordered_map<size_t, ManagedUnorderedMap<size_t,size_t> > _Nks;
    unordered_map<size_t, ManagedUnorderedMap<size_t,double> > _NNks;
};


struct get_communities_selector
{
    get_communities_selector(GraphInterface::comm_corr_t corr):_corr(corr) {}
    GraphInterface::comm_corr_t _corr;

    template <class Graph, class WeightMap, class CommunityMap>
    void operator()(Graph& g, WeightMap weights, CommunityMap s, double gamma, size_t n_iter, pair<double,double> Tinterval, size_t Nspins, size_t seed, bool verbose) const
    {
	switch (_corr)
	{
	case GraphInterface::ERDOS_REYNI:
	    get_communities<NNKSErdosReyni>()(g, weights, s, gamma, n_iter, Tinterval, Nspins, seed, verbose);
	    break;
	case GraphInterface::UNCORRELATED:
	    get_communities<NNKSUncorr>()(g, weights, s, gamma, n_iter, Tinterval, Nspins, seed, verbose);
	    break;
	case GraphInterface::CORRELATED:
	    get_communities<NNKSCorr>()(g, weights, s, gamma, n_iter, Tinterval, Nspins, seed, verbose);
	    break;
	}
    }
};

void GraphInterface::GetCommunityStructure(double gamma, comm_corr_t corr, size_t n_iter, double Tmin, double Tmax, size_t Nspins, size_t seed, bool verbose, string weight, string property)
{
    typedef HashedDescriptorMap<vertex_index_map_t,size_t> comm_map_t;
    comm_map_t comm_map(_vertex_index);

    bool directed = _directed;
    _directed = false;

    if(weight != "")
    {
	try 
	{
	    dynamic_property_map& weight_prop = find_property_map(_properties, weight, typeid(graph_traits<multigraph_t>::edge_descriptor));
	    if (get_static_property_map<vector_property_map<double,edge_index_map_t> >(&weight_prop))
	    {
		vector_property_map<double,edge_index_map_t> weight_map = 
		    get_static_property_map<vector_property_map<double,edge_index_map_t> >(weight_prop);
		check_filter(*this, bind<void>(get_communities_selector(corr), _1, var(weight_map), var(comm_map), gamma, n_iter, make_pair(Tmin, Tmax), Nspins, seed, verbose),
			     reverse_check(), always_undirected());
	    }
	    else
	    {
		DynamicPropertyMapWrap<double,graph_traits<multigraph_t>::edge_descriptor> weight_map(weight_prop);
		check_filter(*this, bind<void>(get_communities_selector(corr), _1, var(weight_map), var(comm_map), gamma, n_iter, make_pair(Tmin, Tmax), Nspins, seed, verbose),
			     reverse_check(), always_undirected());
	    }
	}
	catch (property_not_found& e)
	{
	    throw GraphException("error getting scalar property: " + string(e.what()));
	}
    }
    else
    {
	ConstantPropertyMap<double,graph_traits<multigraph_t>::edge_descriptor> weight_map(1.0);
	check_filter(*this, bind<void>(get_communities_selector(corr), _1, var(weight_map), var(comm_map), gamma, n_iter, make_pair(Tmin, Tmax), Nspins, seed, verbose),
		     reverse_check(), always_undirected());
    }
    _directed = directed;

    try
    {
	find_property_map(_properties, property, typeid(graph_traits<multigraph_t>::edge_descriptor));
	RemoveVertexProperty(property);
    }
    catch (property_not_found) {}

    _properties.property(property, comm_map);
}

//==============================================================================
// GetModularity()
// get Newman's modularity of a given community partition
//==============================================================================

struct get_modularity
{
    template <class Graph, class WeightMap, class CommunityMap>
    void operator()(Graph& g, WeightMap weights, CommunityMap s, double& modularity) const
    {
	modularity = 0.0;
	
	size_t E = 0;
	double W = 0;

	typename graph_traits<Graph>::edge_iterator e, e_end;
	for (tie(e,e_end) = edges(g); e != e_end; ++e)
	    if (target(*e,g) != source(*e,g))
	    {
		W += get(weights, *e);
		E++;
		if (get(s, target(*e,g)) == get(s, source(*e,g)))
		    modularity += 2*get(weights, *e);
	    }

	unordered_map<size_t, size_t> Ks;

	typename graph_traits<Graph>::vertex_iterator v, v_end;
	for (tie(v,v_end) = vertices(g); v != v_end; ++v)
	    Ks[get(s, *v)] += out_degree_no_loops(*v, g);
	
	for (typeof(Ks.begin()) iter = Ks.begin(); iter != Ks.end(); ++iter)
	    modularity -= (iter->second*iter->second)/double(2*E);

	modularity /= 2*W;
    }
};

double GraphInterface::GetModularity(string weight, string property)
{
    double modularity = 0;

    bool directed = _directed;
    _directed = false;
    try
    {
	dynamic_property_map& comm_prop = find_property_map(_properties, property, typeid(graph_traits<multigraph_t>::vertex_descriptor));
	DynamicPropertyMapWrap<size_t,graph_traits<multigraph_t>::vertex_descriptor> comm_map(comm_prop);
	if(weight != "")
	{
	    dynamic_property_map& weight_prop = find_property_map(_properties, weight, typeid(graph_traits<multigraph_t>::edge_descriptor));
	    DynamicPropertyMapWrap<double,graph_traits<multigraph_t>::edge_descriptor> weight_map(weight_prop);
	    check_filter(*this, bind<void>(get_modularity(), _1, var(weight_map), var(comm_map), var(modularity)),
	    reverse_check(), always_undirected());	    
	}
	else
	{
	    ConstantPropertyMap<double,graph_traits<multigraph_t>::edge_descriptor> weight_map(1.0);
	    check_filter(*this, bind<void>(get_modularity(), _1, var(weight_map), var(comm_map), var(modularity)),
	    reverse_check(), always_undirected());
	}
    }
    catch (property_not_found& e)
    {
	throw GraphException("error getting scalar property: " + string(e.what()));
    }
    _directed = directed;

    return modularity;
}
