// Copyright (c) 2018-2021  Robert J. Hijmans
//
// This file is part of the "spat" library.
//
// spat is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// spat is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with spat. If not, see <http://www.gnu.org/licenses/>.

//#include <vector>
#include "spatRasterMultiple.h"
#include "recycle.h"
#include "vecmath.h"
//#include "vecmath.h"
#include <cmath>
#include "math_utils.h"
#include "file_utils.h"
#include "string_utils.h"


/*
std::vector<double> flat(std::vector<std::vector<double>> v) {
    unsigned s1 = v.size();
    unsigned s2 = v[0].size();

	std::size_t s = s1 * s2;
    std::vector<double> result(s);
    for (size_t i=0; i<s1; i++) {
		for (size_t j=0; j<s2; j++) {
			result[i*s2+j] = v[i][j];
		}
	}
	return result;
}
*/

bool SpatRaster::get_aggregate_dims(std::vector<unsigned> &fact, std::string &message ) {

	unsigned fs = fact.size();
	if ((fs > 3) | (fs == 0)) {
		message = "argument 'fact' should have length 1, 2, or 3";
		return false;
	}
	auto min_value = *std::min_element(fact.begin(),fact.end());
	if (min_value < 1) {
		message = "values in argument 'fact' should be > 0";
		return false;
	}
	auto max_value = *std::max_element(fact.begin(),fact.end());
	if (max_value == 1) {
		message = "all values in argument 'fact' are 1, nothing to do";
		return false;
	}

	fact.resize(6);
	if (fs == 1) {
		fact[1] = fact[0];
		fact[2] = 1;
	} else if (fs == 2) {
		fact[2] = 1;
	}
	// int dy = dim[0], dx = dim[1], dz = dim[2];
	fact[0] = fact[0] < 1 ? 1 : fact[0];
	fact[0] = fact[0] > nrow() ? nrow() : fact[0];
	fact[1] = fact[1] < 1 ? 1 : fact[1];
	fact[1] = fact[1] > ncol() ? ncol() : fact[1];
	fact[2] = std::max(unsigned(1), std::min(fact[2], nlyr()));
	// new dimensions: rows, cols, lays
	fact[3] = std::ceil(double(nrow()) / fact[0]);
	fact[4] = std::ceil(double(ncol()) / fact[1]);
	fact[5] = std::ceil(double(nlyr()) / fact[2]);
	return true;
}


std::vector<unsigned> SpatRaster::get_aggregate_dims2(std::vector<unsigned> fact) {
	// for use with R
	std::string message = "";
	get_aggregate_dims(fact, message);
	return(fact);
}


std::vector<std::vector<double>> SpatRaster::get_aggregates(std::vector<double> &in, size_t nr, std::vector<unsigned> dim) {

// dim 0, 1, 2, are the aggregations factors dy, dx, dz
// and 3, 4, 5 are the new nrow, ncol, nlyr

// adjust for chunk
	//dim[3] = std::ceil(double(nr) / dim[0]);
	//size_t bpC = dim[3];
	size_t bpC = std::ceil(double(nr) / dim[0]);

	size_t dy = dim[0], dx = dim[1], dz = dim[2];
	size_t bpR = dim[4];
	size_t bpL = bpR * bpC;

	// new number of layers
	size_t newNL = dim[5];

	// new number of rows, adjusted for additional (expansion) rows
	size_t adjnr = bpC * dy;

	// number of aggregates
	size_t nblocks = (bpR * bpC * newNL);
	// cells per aggregate
	size_t blockcells = dx * dy * dz;

	// output: each row is a block
	std::vector< std::vector<double> > a(nblocks, std::vector<double>(blockcells, std::numeric_limits<double>::quiet_NaN()));

    size_t nc = ncol();
    // size_t ncells = ncell();
    size_t ncells = nr * nc;
    size_t nl = nlyr();
    size_t lstart, rstart, cstart, lmax, rmax, cmax, f, lj, cell;

	for (size_t b = 0; b < nblocks; b++) {
		lstart = dz * (b / bpL);
		rstart = (dy * (b / bpR)) % adjnr;
		cstart = dx * (b % bpR);

		lmax = std::min(nl, (lstart + dz));
		rmax = std::min(nr, (rstart + dy));  // nrow -> nr
		cmax = std::min(nc, (cstart + dx));

		f = 0;
		for (size_t j = lstart; j < lmax; j++) {
			lj = j * ncells;
			for (size_t r = rstart; r < rmax; r++) {
				cell = lj + r * nc;
				for (size_t c = cstart; c < cmax; c++) {
					a[b][f] = in[cell + c];
					f++;
				}
			}
		}
	}
	return(a);
}


std::vector<double> compute_aggregates(std::vector<double> &in, size_t nr, size_t nc, size_t nl, std::vector<unsigned> dim, std::function<double(std::vector<double>&, bool)> fun, bool narm) {

// dim 0, 1, 2, are the aggregations factors dy, dx, dz
// and 3, 4, 5 are the new nrow, ncol, nlyr

	size_t dy = dim[0], dx = dim[1], dz = dim[2];
//	size_t bpC = dim[3];
// adjust for chunk
	size_t bpC = std::ceil(double(nr) / dim[0]);
	size_t bpR = dim[4];
	size_t bpL = bpR * bpC;

	// new number of layers
	size_t newNL = dim[5];

	// new number of rows, adjusted for additional (expansion) rows
	size_t adjnr = bpC * dy;

	// number of aggregates
	size_t nblocks = (bpR * bpC * newNL);
	// cells per aggregate
	size_t blockcells = dx * dy * dz;

	// output: each row is a block
	std::vector<double> out(nblocks, NAN);

//    size_t nl = nlyr();
//    size_t nc = ncol();
    size_t ncells = nr * nc;
    size_t lstart, rstart, cstart, lmax, rmax, cmax, f, lj, cell;

	for (size_t b = 0; b < nblocks; b++) {
		lstart = dz * (b / bpL);
		rstart = (dy * (b / bpR)) % adjnr;
		cstart = dx * (b % bpR);

		lmax = std::min(nl, (lstart + dz));
		rmax = std::min(nr, (rstart + dy));  // nrow -> nr
		cmax = std::min(nc, (cstart + dx));

		f = 0;
		std::vector<double> a(blockcells, NAN);
		for (size_t j = lstart; j < lmax; j++) {
			lj = j * ncells;
			for (size_t r = rstart; r < rmax; r++) {
				cell = lj + r * nc;
				for (size_t c = cstart; c < cmax; c++) {
					a[f] = in[cell + c];
					f++;
				}
			}
		}
		out[b] = fun(a, narm);
	}
	return(out);
}



SpatRaster SpatRaster::aggregate(std::vector<unsigned> fact, std::string fun, bool narm, SpatOptions &opt) {

	SpatRaster out;
	std::string message = "";
	bool success = get_aggregate_dims(fact, message);

// fact 0, 1, 2, are the aggregation factors dy, dx, dz
// and  3, 4, 5 are the new nrow, ncol, nlyr
	if (!success) {
		out.setError(message);
		return out;
	}

	SpatExtent extent = getExtent();
	double xmax = extent.xmin + fact[4] * fact[1] * xres();
	double ymin = extent.ymax - fact[3] * fact[0] * yres();
	SpatExtent e = SpatExtent(extent.xmin, xmax, ymin, extent.ymax);
	out = SpatRaster(fact[3], fact[4], fact[5], e, "");
	out.source[0].srs = source[0].srs;
	// there is much more. categories, time. should use geometry and then
	// set extent and row col
	if (fact[5] == nlyr()) {
		out.setNames(getNames());
	}

	if (!source[0].hasValues) { 
		return out; 
	}

	if (!haveFun(fun)) {
		out.setError("unknown function argument");
		return out;
	}

/*
	size_t ifun = std::distance(f.begin(), it);
	std::string gstring = "";
	if (ifun > 0) {
		std::vector<std::string> gf {"average", "min", "max", "med", "mode"};
		gstring = gf[ifun-1]; 
	}

#ifdef useGDAL 
#if GDAL_VERSION_MAJOR >= 3
	if (gstring != "") {
		out = warper(out, "", gstring, opt);
		return out;
	}
#endif
#endif
*/

	std::function<double(std::vector<double>&, bool)> agFun = getFun(fun);

	unsigned outnc = out.ncol();

	//BlockSize bs = getBlockSize(4, opt.get_memfrac());
	BlockSize bs = getBlockSize(opt);
	//bs.n = floor(nrow() / fact[0]); # ambiguous on solaris
	bs.n = std::floor(static_cast <double> (nrow() / fact[0]));

	bs.nrows = std::vector<size_t>(bs.n, fact[0]);
	bs.row.resize(bs.n);
	for (size_t i =0; i<bs.n; i++) {
		bs.row[i] = i * fact[0];
	}
	size_t lastrow = bs.row[bs.n - 1] + bs.nrows[bs.n - 1] + 1;
	if (lastrow < nrow()) {
		bs.row.push_back(lastrow);
		bs.nrows.push_back(std::min(bs.nrows[bs.n-1], nrow()-lastrow));
		bs.n += 1;
	}
	if (!readStart()) {
		out.setError(getError());
		return(out);
	}

	opt.steps = bs.n;
	opt.minrows = fact[0];

	if (fun == "modal") {
		if (nlyr() == out.nlyr()) {
			out.source[0].hasColors = hasColors();
			out.source[0].cols = getColors();
			out.source[0].hasCategories = hasCategories();
			out.source[0].cats = getCategories();
		}
	}
	
	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}

	size_t nc = ncol();
	for (size_t i = 0; i < bs.n; i++) {
        std::vector<double> vin = readValues(bs.row[i], bs.nrows[i], 0, nc);
		std::vector<double> v  = compute_aggregates(vin, bs.nrows[i], nc, nlyr(), fact, agFun, narm);
		if (!out.writeValues(v, i, 1, 0, outnc)) return out;
	}
	out.writeStop();
	readStop();
	return(out);
}




SpatRaster SpatRaster::weighted_mean(SpatRaster w, bool narm, SpatOptions &opt) {
	SpatRaster out;
	if (nlyr() != w.nlyr()) {
		out.setError("nlyr of data and weights are different");
		return out;
	}
	
	SpatOptions topt(opt);
	out = arith(w, "*", topt);
	out = out.summary("sum", narm, topt);
	SpatRaster wsum = w.summary("sum", narm, topt);
	return out.arith(wsum, "/", opt);
	
}


SpatRaster SpatRaster::weighted_mean(std::vector<double> w, bool narm, SpatOptions &opt) {
	SpatOptions topt(opt);
	recycle(w, nlyr());
	SpatRaster out = arith(w, "*", false, topt);
	out = out.summary("sum", narm, topt);
	double wsum = vsum(w, narm);
	return out.arith(wsum, "/", false, opt);
}


SpatRaster SpatRaster::separate(std::vector<double> classes, double keepvalue, double othervalue, SpatOptions &opt) {

	SpatRaster out;
	if (nlyr() > 1) {
		out.setError("input may only have one layer");
		return out;
	}
	if (classes.size() == 0) {
		SpatOptions topt(opt);
		std::vector<std::vector<double>> rc = unique(false, topt);
		classes = rc[0];
	} 

	std::vector<int> uc(classes.size());
	for (size_t i=0; i<classes.size(); i++) {
		uc[i] = round(classes[i]);
	}
	std::sort(uc.begin(), uc.end());
	uc.erase(std::unique(uc.begin(), uc.end()), uc.end());

	size_t n = uc.size();
	if (n == 0) {
		out.setError("no valid classes");
		return out;	
	}
	out = geometry(n);
	std::vector<std::string> snms(n);
	for (size_t i=0; i<n; i++) {
		snms[i] = std::to_string(uc[i]);
	}
	out.setNames(snms);
	
	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
  	if (!out.writeStart(opt)) { 
		readStop();
		return out; 
	}

	for (size_t i = 0; i < out.bs.n; i++) {
		std::vector<double> v = readBlock(out.bs, i);
		size_t nn = v.size();
		std::vector<double> vv(nn * n, NAN);
		for (size_t j=0; j<nn; j++) {
			if (!std::isnan(v[j])) {
				for (size_t k=0; k<uc.size(); k++) {
					if (v[j] == uc[k]) {
						if (keepvalue) {
							vv[j + k*nn] = uc[k];
						} else {
							vv[j + k*nn] = 1;	 // true					
						}
					} else {
						vv[j + k*nn] = othervalue;					
					}
				}
			
			}
		} 
		if (!out.writeValues(vv, out.bs.row[i], out.bs.nrows[i], 0, ncol())) {
			readStop();
			return out;
		}
	}
	readStop();
	out.writeStop();
	return(out);
}


SpatRaster SpatRaster::is_in(std::vector<double> m, SpatOptions &opt) {

	SpatRaster out = geometry();
	if (m.size() == 0) {
		out.setError("no matches supplied");
		return(out);
	}
	if (!hasValues()) {
		out.setError("input has no values");
		return(out);
	}

	int hasNAN = 0;
	for (size_t i=0; i<m.size(); i++) {
		if (std::isnan(m[i])) {
			hasNAN = 1;
			m.erase(m.begin()+i);
			break;
		}
	}
	if (m.size() == 0) { // only NA
		return isnan(opt);
	}


	// if m is very long, perhaps first check if the value is in range?

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}

  	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	for (size_t i = 0; i < out.bs.n; i++) {
		std::vector<double> v = readBlock(out.bs, i);
		std::vector<double> vv(v.size(), 0);
		for (size_t j=0; j<v.size(); j++) {
			if (std::isnan(v[j])) {
				vv[j] = hasNAN;
			} else {
				for (size_t k=0; k<m.size(); k++) {
					if (v[j] == m[k]) {
						vv[j] = 1;
						break;
					}
				}
			}
		} 
	
		if (!out.writeValues(vv, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
	}
	readStop();
	out.writeStop();
	return(out);
}



std::vector<std::vector<double>> SpatRaster::is_in_cells(std::vector<double> m, SpatOptions &opt) {

	std::vector<std::vector<double>> out(nlyr());

	if (m.size() == 0) {
		return(out);
	}
	if (!hasValues()) {
		return(out);
	}
	bool hasNAN = false;
	for (size_t i=0; i<m.size(); i++) {
		if (std::isnan(m[i])) {
			hasNAN = true;
			m.erase(m.begin()+i);
			break;
		}
	}
//	if (m.size() == 0) { // only NA
		//nanOnly=true;
//	}

	if (!readStart()) {
		return(out);
	}

	BlockSize bs = getBlockSize(opt);
	size_t nc = ncol();
	for (size_t i = 0; i < bs.n; i++) {
		std::vector<double> v = readBlock(bs, i);
		size_t cellperlayer = bs.nrows[i] * nc; 
		for (size_t j=0; j<v.size(); j++) {
			size_t lyr = j / cellperlayer;
			size_t cell = j % cellperlayer + bs.row[i] * nc;
			if (std::isnan(v[j])) {
				if (hasNAN)	out[lyr].push_back(cell);
			} else {
				for (size_t k=0; k<m.size(); k++) {
					if (v[j] == m[k]) {
						out[lyr].push_back(cell);
						break;
					}
				}
			}
		} 
	}
	readStop();
	return(out);
}




SpatRaster SpatRaster::stretch(std::vector<double> minv, std::vector<double> maxv, std::vector<double> minq, std::vector<double> maxq, std::vector<double> smin, std::vector<double> smax, SpatOptions &opt) {

	SpatRaster out = geometry();
	if (!hasValues()) return(out);

	size_t nl = nlyr();
	recycle(minv, nl);
	recycle(maxv, nl);
	recycle(minq, nl);
	recycle(maxq, nl);
	recycle(smin, nl);
	recycle(smax, nl);

	std::vector<std::vector<double>> q(nl);
	std::vector<bool> useS(nl, false);
	std::vector<double> mult(nl);

	for (size_t i=0; i<nl; i++) {
		if (minv[i] >= maxv[i]) {
			out.setError("maxv must be larger than minv");
			return out;
		}
		if ((!std::isnan(smin[i])) && (!std::isnan(smax[i]))) {
			if (smin[i] >= smax[i]) {
				out.setError("smax must be larger than smin");
				return out;
			}
			useS[i] = true;
			q[i] = {smin[i], smax[i]};
		} else {
			if (minq[i] >= maxq[i]) {
				out.setError("maxq must be larger than minq");
				return out;
			}
			if ((minq[i] < 0) || (maxq[i] > 1)) {
				out.setError("minq and maxq must be between 0 and 1");
				return out;		
			}
		}
	}

	std::vector<bool> hR = hasRange();
	for (size_t i=0; i<nl; i++) {
		if (!useS[i]) {
			if ((minq[i]==0) && (maxq[i]==1) && hR[i]) {
				std::vector<double> rmn = range_min(); 
				std::vector<double> rmx = range_max(); 
				q[i] = {rmn[i], rmx[i]};
			} else {
				std::vector<double> probs = {minq[i], maxq[i]};
				std::vector<double> v = getValues(i);
				q[i] = vquantile(v, probs, true);
			}
		}
		mult[i] = maxv[i] / (q[i][1]-q[i][0]);
	}

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}

  	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	for (size_t i = 0; i < out.bs.n; i++) {
		std::vector<double> v = readBlock(out.bs, i);
		size_t nc = out.bs.nrows[i] * ncol();
		for (size_t j=0; j<v.size(); j++) {
			size_t lyr = j / nc;
			v[j] = mult[lyr] * (v[j] - q[lyr][0]);
			if (v[j] < minv[lyr]) v[j] = minv[lyr];
			if (v[j] > maxv[lyr]) v[j] = maxv[lyr];
		}
		if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
	}
	readStop();
	out.writeStop();

	return(out);
}


SpatRaster SpatRaster::apply(std::vector<unsigned> ind, std::string fun, bool narm, std::vector<std::string> nms, SpatOptions &opt) {

	recycle(ind, nlyr());
	std::vector<unsigned> ui = vunique(ind);
	unsigned nl = ui.size();
	SpatRaster out = geometry(nl);
	recycle(nms, nl);
	out.setNames(nms);

	if (!haveFun(fun)) {
		out.setError("unknown function argument");
		return out;
	}

	if (!hasValues()) return(out);

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
 	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	out.bs = getBlockSize(opt);
//	#ifdef useRcpp
//	out.pbar = new Progress(out.bs.n+2, opt.show_progress(bs.n));
//	out.pbar->increment();
//	#endif

	std::vector<std::vector<double>> v(nl);
	std::vector<unsigned> ird(ind.size());
	std::vector<unsigned> jrd(ind.size());
	for (size_t i=0; i<nl; i++) {
		for (size_t j=0; j<ind.size(); j++) {
			if (ui[i] == ind[j]) {
				v[i].push_back(0);
				ird[j] = i;
				jrd[j] = v[i].size()-1;
			}
		}
	}

	std::function<double(std::vector<double>&, bool)> theFun = getFun(fun);

	for (size_t i=0; i<out.bs.n; i++) {
        std::vector<double> a = readBlock(out.bs, i);
		unsigned nc = out.bs.nrows[i] * ncol();
		std::vector<double> b(nc * nl);
		for (size_t j=0; j<nc; j++) {
			for (size_t k=0; k<ird.size(); k++) {
				v[ird[k]][jrd[k]] = a[j+k*nc];
			}
			for (size_t k=0; k<ui.size(); k++) {
				size_t off = k * nc + j;
				b[off] = theFun(v[k], narm);
			}
		}
		if (!out.writeValues(b, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
	}
	readStop();
	out.writeStop();
	return(out);
}



SpatRaster SpatRaster::mask(SpatRaster x, bool inverse, double maskvalue, double updatevalue, SpatOptions &opt) {

	unsigned nl = std::max(nlyr(), x.nlyr());
	SpatRaster out = geometry(nl, true);

	if (!out.compare_geom(x, false, true, opt.get_tolerance(), true, true, true, false)) {
		return(out);
	}

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
	if (!x.readStart()) {
		out.setError(x.getError());
		return(out);
	}
  	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	std::vector<double> v, m;
	for (size_t i = 0; i < out.bs.n; i++) {
		v = readValues(out.bs.row[i], out.bs.nrows[i], 0, ncol());
		m = x.readValues(out.bs.row[i], out.bs.nrows[i], 0, ncol());
		recycle(v, m);
		if (inverse) {
			if (std::isnan(maskvalue)) {
				for (size_t i=0; i < v.size(); i++) {
					if (!std::isnan(m[i])) {
						v[i] = updatevalue;
					}
				}
			} else {
				for (size_t i=0; i < v.size(); i++) {
					if (m[i] != maskvalue) {
						v[i] = updatevalue;
					}
				}
			}
		} else {
			if (std::isnan(maskvalue)) {
				for (size_t i=0; i < v.size(); i++) {
					if (std::isnan(m[i])) {
						v[i] = updatevalue;
					}
				}
			} else {
				for (size_t i=0; i < v.size(); i++) {
					if (m[i] == maskvalue) {
						v[i] = updatevalue;
					}
				}
			}
		}
		if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;

	}
	out.writeStop();
	readStop();
	x.readStop();
	return(out);
}



SpatRaster SpatRaster::mask(SpatRaster x, bool inverse, std::vector<double> maskvalues, double updatevalue, SpatOptions &opt) {

	maskvalues = vunique(maskvalues);
	if (maskvalues.size() == 1) {
		return mask(x, inverse, maskvalues[0], updatevalue, opt);
	}

	unsigned nl = std::max(nlyr(), x.nlyr());
	SpatRaster out = geometry(nl, true);

	if (!out.compare_geom(x, false, true, opt.get_tolerance(), true, true, true, false)) {
		return(out);
	}

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
	if (!x.readStart()) {
		out.setError(x.getError());
		return(out);
	}

	bool maskNA = false;
	for (int i = maskvalues.size()-1; i>=0; i--) {
		if (std::isnan(maskvalues[i])) {
			maskNA = true;
			maskvalues.erase(maskvalues.begin()+i);
		}
	}

  	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	std::vector<double> v, m;
	for (size_t i = 0; i < out.bs.n; i++) {
		v = readValues(out.bs.row[i], out.bs.nrows[i], 0, ncol());
		m = x.readValues(out.bs.row[i], out.bs.nrows[i], 0, ncol());
		recycle(v, m);
		if (inverse) {
			for (size_t i=0; i < v.size(); i++) {
				if (maskNA && std::isnan(m[i])) {
					v[i] = updatevalue;
				} else {
					for (size_t j=0; j < maskvalues.size(); j++) {
						if (m[i] != maskvalues[j]) {
							v[i] = updatevalue;
							break;
						}
					}
				}
			}
		} else {
			for (size_t i=0; i < v.size(); i++) {
				if (maskNA && std::isnan(m[i])) {
					v[i] = updatevalue;
				} else {
					for (size_t j=0; j < maskvalues.size(); j++) {
						if (m[i] == maskvalues[j]) {
							v[i] = updatevalue;
							break;
						}
					}
				}
			}
		}
		if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;

	}
	out.writeStop();
	readStop();
	x.readStop();
	return(out);
}


SpatRaster SpatRaster::mask(SpatVector x, bool inverse, double updatevalue, bool touches, SpatOptions &opt) {

	SpatRaster out;
	if (!hasValues()) {
		out.setError("SpatRaster has no values");
		return out;
	}
	if (inverse) {
		out = rasterizeLyr(x, updatevalue, NAN, touches, true, opt);
	} else {
		SpatOptions topt(opt);
		out = rasterizeLyr(x, 1, 0, touches, false, topt);
		if (out.hasError()) {
			return out;
		}
		if (std::isnan(updatevalue)) {
			out = mask(out, false, 0, updatevalue, opt);
		} else {
			out = mask(out, false, 0, updatevalue, topt);
			out = out.mask(*this, false, NAN, NAN, opt);
		}
	}
	return(out);
}



SpatRaster SpatRaster::transpose(SpatOptions &opt) {

	SpatRaster out = geometry(nlyr(), true);
	SpatExtent eold = getExtent();
	SpatExtent enew = getExtent();
	enew.xmin = eold.ymin;
	enew.xmax = eold.ymax;
	enew.ymin = eold.xmin;
	enew.ymax = eold.xmax;
	out.setExtent(enew, false, "");
	out.source[0].ncol = nrow();
	out.source[0].nrow = ncol();
	if (!hasValues()) return out;
	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
 	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	for (size_t i=0; i < out.bs.n; i++) {
		unsigned nr = nrow();
		unsigned nc = out.bs.nrows[i];
		std::vector<double> v = readValues(0, nr, out.bs.row[i], nc);
		std::vector<double> vv(v.size());
		for (size_t lyr=0; lyr<nlyr(); lyr++) {
			size_t off = lyr*ncell();
			for (size_t r = 0; r < nr; r++) {
				size_t rnc = off + r * nc;
				for (size_t c = 0; c < nc; c++) {
					vv[c*nr+r+off] = v[rnc+c];
				}
			}
		}
		if (!out.writeValues(vv, out.bs.row[i], out.bs.nrows[i], 0, out.ncol())) return out;
	}
	out.writeStop();
	readStop();
	return(out);
}





SpatRaster SpatRaster::trim(double value, unsigned padding, SpatOptions &opt) {

	long nrl = nrow() * nlyr();
	long ncl = ncol() * nlyr();

	std::vector<double> v;
	size_t r;
	size_t nr = nrow();
	bool rowfound = false;
	if (!readStart()) {
		SpatRaster out;
		out.setError(getError());
		return(out);
	}


	size_t firstrow, lastrow, firstcol, lastcol;
	if (std::isnan(value)) {
		for (r=0; r<nr; r++) {
			v = readValues(r, 1, 0, ncol());
			if (std::count_if( v.begin(), v.end(), [](double d) { return std::isnan(d); } ) < ncl) {
				rowfound = true;
				break;
			}
		}

		if (!rowfound) { 
			SpatRaster out;
			out.setError("only cells with NA found");
			return out;
		}

		firstrow = std::max(r - padding, size_t(0));

		for (r=nrow()-1; r>firstrow; r--) {
			v = readValues(r, 1, 0, ncol());
			if (std::count_if(v.begin(), v.end(), [](double d) { return std::isnan(d); } ) < ncl) {
				break;
			}
		}

		lastrow = std::max(std::min(r+padding, nrow()), size_t(0));

		if (lastrow < firstrow) {
			std::swap(firstrow, lastrow);
		}
		size_t c;
		for (c=0; c<ncol(); c++) {
			v = readValues(0, nrow(), c, 1);
			if (std::count_if( v.begin(), v.end(), [](double d) { return std::isnan(d); } ) < nrl) {
				break;
			}
		}
		firstcol = std::min(std::max(c-padding, size_t(0)), ncol());

		for (c=ncol()-1; c>firstcol; c--) {
			v = readValues(0, nrow(), c, 1);
			if (std::count_if( v.begin(), v.end(), [](double d) { return std::isnan(d); } ) < nrl) {
				break;
			}
		}
		lastcol = std::max(std::min(c+padding, ncol()), size_t(0));
	} else {	
		for (r=0; r<nr; r++) {
			v = readValues(r, 1, 0, ncol());
			if (std::count( v.begin(), v.end(), value) < ncl) {
				rowfound = true;
				break;
			}
		}

		if (!rowfound) { 
			SpatRaster out;
			out.setError("only cells with value: " + std::to_string(value) + " found");
			return out;
		}

		firstrow = std::max(r - padding, size_t(0));

		for (r=nrow()-1; r>firstrow; r--) {
			v = readValues(r, 1, 0, ncol());
			if (std::count( v.begin(), v.end(), value) < ncl) {
				break;
			}
		}

		lastrow = std::max(std::min(r+padding, nrow()), size_t(0));

		if (lastrow < firstrow) {
			std::swap(firstrow, lastrow);
		}
		size_t c;
		for (c=0; c<ncol(); c++) {
			v = readValues(0, nrow(), c, 1);
			if (std::count( v.begin(), v.end(), value) < nrl) {
				break;
			}
		}
		firstcol = std::min(std::max(c-padding, size_t(0)), ncol());


		for (c=ncol()-1; c>firstcol; c--) {
			v = readValues(0, nrow(), c, 1);
			if (std::count( v.begin(), v.end(), value) < nrl) {
				break;
			}
		}
		lastcol = std::max(std::min(c+padding, ncol()), size_t(0));

	}
	readStop();
	if (lastcol < firstcol) {
		std::swap(firstcol, lastcol);
	}

	std::vector<double> res = resolution();
	double xr = res[0];
	double yr = res[1];
	SpatExtent e = SpatExtent(xFromCol(firstcol)-0.5*xr, xFromCol(lastcol)+0.5*xr, yFromRow(lastrow)-0.5*yr, yFromRow(firstrow)+0.5*yr);

	return( crop(e, "near", opt) ) ;
}





void clamp_vector(std::vector<double> &v, double low, double high, bool usevalue) {
	size_t n = v.size();
	if (usevalue) {
		for (size_t i=0; i<n; i++) {
			if ( v[i] < low ) {
				v[i] = low;
			} else if ( v[i] > high ) {
				v[i] = high;
			}
		}
	} else {
		for (size_t i=0; i<n; i++) {
			if ( (v[i] < low )| (v[i] > high)) {
				v[i] = NAN;
			}
		}
	}
}



SpatRaster SpatRaster::clamp(double low, double high, bool usevalue, SpatOptions &opt) {

	SpatRaster out = geometry(nlyr(), true);
	if (low > high) {
		out.setError("lower clamp value cannot be larger than the higher clamp value");
		return out;
	}
	if (!hasValues()) {
		out.setError("cannot clamp a raster with no values");
		return out;
	}

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}

  	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	for (size_t i = 0; i < out.bs.n; i++) {
		std::vector<double> v = readBlock(out.bs, i);
		clamp_vector(v, low, high, usevalue);
		if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
	}
	readStop();
	out.writeStop();
	return(out);
}




SpatRaster SpatRaster::selRange(SpatRaster x, int z, int recycleby, SpatOptions &opt) {

	int nl = nlyr();
	z = std::max(1, std::min(z, nl));
	size_t nrecs = 1;
	if (recycleby > 1 && recycleby < nl) {
		nrecs = nl / recycleby;
	} else {
		recycleby = 0;
	}
	SpatRaster out = geometry( z * nrecs );
	if (!out.compare_geom(x, false, false, opt.get_tolerance())) {
		return(out);
	}
	if (!hasValues()) return(out);

	if (x.nlyr() > 1) {
		out.setError("index raster must have only one layer");
		return out;
	}
	if (!x.hasValues()) {
		out.setError("index raster has no values");
		return out;
	}

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
	if (!x.readStart()) {
		out.setError(x.getError());
		return(out);
	}
 
	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	for (size_t i=0; i<out.bs.n; i++) {
		std::vector<double> v = readBlock(out.bs, i);
		std::vector<double> idx = x.readBlock(out.bs, i);
		size_t is = idx.size();
		std::vector<double> vv(is*z*nrecs, NAN);
		size_t ncell = out.bs.nrows[i] * ncol(); // same as is?

		for (size_t j=0; j<is; j++) {  //index.size (each cell)
			for (size_t k=0; k<nrecs; k++) {
				int start = idx[j] - 1 + k * recycleby;  // first layer for this cell
				int offbase = (k*z) * ncell;
				if ((start >= 0) && (start < nl)) {
					int zz = std::min(nl-start, z); // do not surpass the last layer
					for (int i=0; i<zz; i++){
						size_t offin = (start + i) * ncell + j;
						size_t offout = offbase + i * ncell + j;
						vv[offout] = v[offin];   
					}
				}
			}
		}
		//for (size_t j=0; j<is; j++) {
		//	int index = idx[j] - 1;
		//	if ((index >= 0) && (index < nl)) {
		//		vv[j] = v[j + index * ncell];
		//	}
		//}
		if (!out.writeValues(vv, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
	}
	readStop();
	x.readStop();
	out.writeStop();
	return(out);
}


SpatRaster SpatRaster::rapply(SpatRaster x, double first, double last, std::string fun, bool clamp, bool narm, SpatOptions &opt) {

	SpatRaster out = geometry(1);
	if (!haveFun(fun)) {
		out.setError("unknown function argument");
		return out;
	}

	bool sval = !std::isnan(first);
	bool eval = !std::isnan(last);
	if (sval && eval) {
		out.setError("arguments `first` or `last` must be NA. See `app` for other cases");
		return out;		
	}
	int start = sval ? first-1 : -99;
	int end = eval ? last-1 : -999;

	if (!out.compare_geom(x, false, false, opt.get_tolerance())) {
		return(out);
	}
	if (!x.hasValues()) {
		out.setError("index raster has no values");
		return out;
	}
	unsigned expnl = 2 - (sval + eval);
	if (x.nlyr() != expnl) {
		out.setError("index raster must have " + std::to_string(expnl) + "layer(s)");
		return out;
	}
	if (!hasValues()) {
		out.setError("no values in input");
		return(out);
	}

	std::function<double(std::vector<double>&, bool)> theFun = getFun(fun);

	int nl = nlyr();
 	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
	if (!x.readStart()) {
		out.setError(x.getError());
		return(out);
	}
	
	for (size_t i=0; i<out.bs.n; i++) {
		std::vector<double> v = readBlock(out.bs, i);
		std::vector<double> idx = x.readBlock(out.bs, i);
		size_t ncell = out.bs.nrows[i] * ncol();
		std::vector<double> vv(ncell, NAN);
		for (size_t j=0; j<ncell; j++) {
			if (std::isnan(idx[j])) continue;
			if (sval) {
				end = idx[j] - 1;	
			} else if (eval) {
				start = idx[j] - 1;
			} else {
				start = idx[j] - 1;
				double dend = idx[j+ncell]-1;
				if (std::isnan(dend)) continue;
				end   = dend;
			}
			if (clamp) {
				start = start < 0 ? 0 : start; 
				end = end >= nl ? (nl-1) : end; 
			}
			if ((start <= end) && (end < nl) && (start >= 0)) {
				std::vector<double> se;
				se.reserve(end-start+1);
				for (int k = start; k<=end; k++){
					size_t off = k * ncell + j;
					se.push_back(v[off]);   
				}
				vv[j] = theFun(se, narm);
			} 
		}
		if (!out.writeValues(vv, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
	}
	readStop();
	x.readStop();
	out.writeStop();
	return(out);
}


std::vector<std::vector<double>> SpatRaster::rappvals(SpatRaster x, double first, double last, bool clamp, bool all, double fill, size_t startrow, size_t nrows) {

	std::vector<std::vector<double>> r;

	bool sval = !std::isnan(first);
	bool eval = !std::isnan(last);
	if (sval && eval) {
		setError("first or last must be NA. See `app` for other cases");
		return r;		
	}
	int start = sval ? first-1 : 0;
	int end = eval ? last-1 : 0;

	if (!compare_geom(x, false, false, 0.1)) {
		return(r);
	}
	if (!hasValues()) {
		return r;
	}
	if (!x.hasValues()) {
		setError("index raster has no values");
		return r;
	}
	unsigned expnl = 2 - (sval + eval);
	if (x.nlyr() != expnl) {
		setError("index raster must have " + std::to_string(expnl) + "layer(s)");
		return r;
	}

	int nl = nlyr();
	if (!readStart()) {
		return(r);
	}
	if (!x.readStart()) {
		setError(x.getError());
		return(r);
	}

	std::vector<double> v = readValues(startrow, nrows, 0, ncol());
	std::vector<double> idx = x.readValues(startrow, nrows, 0, ncol());
	size_t ncell = nrows * ncol();
	r.resize(ncell);
	
	for (size_t j=0; j<ncell; j++) {
		if (std::isnan(idx[j])) {
			if (all) {
				r[j].resize(nl, NAN);			
			} else {
				r[j].push_back(NAN);
			}
			continue;
		}
		if (sval) {
			end = idx[j] - 1;	
			//end = idx[j];	
		} else if (eval) {
			start = idx[j] - 1;
		} else {
			start = idx[j] - 1;
			//double dend = idx[j+ncell];
			double dend = idx[j+ncell]-1;
			end = std::isnan(dend) ? -999 : (int) dend;
		}
		if (clamp) {
			start = start < 0 ? 0 : start; 
			end = end >= nl ? (nl-1) : end; 
		}

		bool inrange = (start <= end) && (end < nl) && (start >= 0);
		if (all) {
			if (inrange) {
				r[j].resize(nl, fill);
				for (int k = start; k<=end; k++){
					size_t off = k * ncell + j;
					r[j][k] = v[off];   
				}		
			} else {
				r[j].resize(nl, NAN);
			}	
		} else if (inrange) {
			r[j].reserve(end-start+1);
			for (int k=start; k<=end; k++){
				size_t off = k * ncell + j;
				r[j].push_back(v[off]);   
			}
		} else {
			r[j].push_back(NAN);
		}
		
	}
	readStop();
	x.readStop();
	return(r);
}


bool disaggregate_dims(std::vector<unsigned> &fact, std::string &message ) {

	unsigned fs = fact.size();
	if ((fs > 3) | (fs == 0)) {
		message = "argument 'fact' should have length 1, 2, or 3";
		return false;
	}
	auto min_value = *std::min_element(fact.begin(),fact.end());
	if (min_value < 1) {
		message = "values in argument 'fact' should be > 0";
		return false;
	}
	auto max_value = *std::max_element(fact.begin(),fact.end());
	if (max_value == 1) {
		message = "all values in argument 'fact' are 1, nothing to do";
		return false;
	}

	fact.resize(3);
	if (fs == 1) {
		fact[1] = fact[0];
	}
	fact[2] = 1;
	return true;
}



SpatRaster SpatRaster::disaggregate(std::vector<unsigned> fact, SpatOptions &opt) {

    SpatRaster out = geometry(nlyr(), true);
	std::string message = "";
	bool success = disaggregate_dims(fact, message);
	if (!success) {
		out.setError(message);
		return out;
	}

    out.source[0].nrow = out.source[0].nrow * fact[0];
    out.source[0].ncol = out.source[0].ncol * fact[1];
    out.source[0].nlyr = out.source[0].nlyr * fact[2];

    if (!hasValues()) {
        return out;
    }

	opt.ncopies = 2*fact[0]*fact[1]*fact[2];
	BlockSize bs = getBlockSize(opt);
	//opt.set_blocksizemp();
	std::vector<double> v, vout;
	unsigned nc = ncol();
	unsigned nl = nlyr();
	std::vector<double> newrow(nc*fact[1]);
	if (!readStart()) {
		out.setError(getError());
		return(out);
	}

  	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	for (size_t i = 0; i < bs.n; i++) {
		v = readValues(bs.row[i], bs.nrows[i], 0, nc);
		vout.resize(0);
		vout.reserve(v.size() * fact[0] * fact[1] * fact[2]);

		for (size_t lyr=0; lyr<nl; lyr++) {
			for (size_t row=0; row<bs.nrows[i]; row++) {
				unsigned rowoff = row*nc + lyr*nc*bs.nrows[i];
				// for each new column
				unsigned jfact = 0;
				for (size_t j=0; j<nc; j++) {
					unsigned coloff = rowoff + j;
					for (size_t k=0; k<fact[1]; k++) {
						newrow[jfact+k] = v[coloff];
					}
					jfact += fact[1];
				}
				// for each new row
				for (size_t j=0; j<fact[0]; j++) {
					vout.insert(vout.end(), newrow.begin(), newrow.end());
				}
			}
		}
		if (!out.writeValues(vout, bs.row[i]*fact[0], bs.nrows[i]*fact[0], 0, out.ncol())) return out;
	}
	vout.resize(0);
	out.writeStop();
	readStop();
	return(out);
}



/*
SpatRaster SpatRaster::oldinit(std::string value, bool plusone, SpatOptions &opt) {

	SpatRaster out = geometry(1);

	std::vector<std::string> f {"row", "col", "cell", "x", "y", "chess"};
	bool test = std::find(f.begin(), f.end(), value) == f.end();
	if (test) {
		out.setError("not a valid init option");
		return out;
	}

	size_t nr = nrow();
	size_t steps = nr; // for the pbar
	if (value == "chess") {
		steps = steps / 2;
	}
	opt.set_steps(steps);
 	if (!out.writeStart(opt)) { return out; }

	if (value == "row") {
		std::vector<double> v(ncol());
		for (size_t i = 0; i < nr; i++) {
			std::fill(v.begin(), v.end(), i+plusone);
			if (!out.writeValues(v, i, 1, 0, ncol())) return out;
		}
	} else if (value == "col") {
		std::vector<double> col(ncol());
		double start = plusone ? 1 : 0;
		std::iota(col.begin(), col.end(), start);
		for (unsigned i = 0; i < nr; i++) {
			if (!out.writeValues(col, i, 1, 0, ncol())) return out;
		}
	} else if (value == "cell") {
		std::vector<long> col(ncol());
		std::iota(col.begin(), col.end(), 0);
		std::vector<long> row(1);
		for (unsigned i = 0; i < nr; i++) {
			row[0] = i;
			std::vector<double> v = cellFromRowCol(row, col);
			if (plusone) for(double& d : v) d=d+1;
			if (!out.writeValues(v, i, 1, 0, ncol())) return out;
		}
	} else if (value == "x") {
		std::vector<long> col(ncol());
		std::iota(col.begin(), col.end(), 0);
		std::vector<double> x = xFromCol(col);
		for (unsigned i = 0; i < nr; i++) {
			if (!out.writeValues(x, i, 1, 0, ncol())) return out;
		}
	} else if (value == "y") {
		std::vector<double> v(ncol());
		for (unsigned i = 0; i < nr; i++) {
			double y = yFromRow(i);
			std::fill(v.begin(), v.end(), y);
			if (!out.writeValues(v, i, 1, 0, ncol())) return out;
		}
	} else if (value == "chess") {
		std::vector<double> a(ncol());
		std::vector<double> b(ncol());
		size_t nr = nrow();
		size_t nc = ncol();
		a[0] = 1;
		b[0] = 0;
		for (size_t i=1; i<nc; i++) {
			bool test = i%2 == 0;
			a[i] = test;
			b[i] = !test;
		}
		out.bs.n = nr/2; // for the pbar
		for (unsigned i=0; i<(nr-1); i=i+2) {
			if (!out.writeValues(a, i, 1, 0, ncol())) return out;
			if (!out.writeValues(b, i+1, 1, 0, ncol())) return out;
		}
		if (nr%2 == 0) {
			if (!out.writeValues(a, nr-2, 1, 0, ncol())) return out;
			if (!out.writeValues(b, nr-1, 1, 0, ncol())) return out;
		} else {
			if (!out.writeValues(a, nr-1, 1, 0, ncol())) return out;
		}
	}

	out.writeStop();
	return(out);
}
*/



SpatRaster SpatRaster::init(std::string value, bool plusone, SpatOptions &opt) {

	SpatRaster out = geometry(1);

	std::vector<std::string> f {"row", "col", "cell", "x", "y", "chess"};
	bool test = std::find(f.begin(), f.end(), value) == f.end();
	if (test) {
		out.setError("not a valid init option");
		return out;
	}

	opt.ncopies = std::max(opt.ncopies, (unsigned) 6);
	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}

	size_t nc = ncol();
	std::vector<double> v;
	if (value == "row") {
		for (size_t i = 0; i < out.bs.n; i++) {
			v.resize(nc * out.bs.nrows[i]);
			for (size_t j = 0; j < out.bs.nrows[i]; j++) {
				size_t r = out.bs.row[i] + j + plusone;
				for (size_t k = 0; k < nc; k++) {
					v[j*nc+k] = r;
				}
			}
			if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, nc)) return out;
		}
	} else if (value == "col") {
		std::vector<double> cnn(nc);
		double start = plusone ? 1 : 0;
		std::iota(cnn.begin(), cnn.end(), start);
		size_t oldnr = 0;
		for (size_t i = 0; i < out.bs.n; i++) {
			if (oldnr != out.bs.nrows[i]) {
				v = cnn;
				recycle(v, out.bs.nrows[i] * nc);
			}
			if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, nc)) return out;
		}
	} else if (value == "cell") {
		for (size_t i = 0; i < out.bs.n; i++) {
			v.resize(nc * out.bs.nrows[i]);
			size_t firstcell = cellFromRowCol(out.bs.row[i], 0);
			firstcell = plusone ? firstcell + 1 : firstcell;
			std::iota(v.begin(), v.end(), firstcell);
			if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, nc)) return out;
		}
	} else if (value == "x") {
		std::vector<int_64> col(nc);
		std::iota(col.begin(), col.end(), 0);
		std::vector<double> xcoords = xFromCol(col);
		size_t oldnr = 0;
		for (size_t i = 0; i < out.bs.n; i++) {
			if (oldnr != out.bs.nrows[i]) {
				v = xcoords;
				recycle(v, out.bs.nrows[i] * nc);
			}
			if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, nc)) return out;
		}
	} else if (value == "y") {
	
		for (size_t i = 0; i < out.bs.n; i++) {
			v.resize(out.bs.nrows[i] * nc );
			for (size_t j = 0; j < out.bs.nrows[i]; j++) {
				double y = yFromRow(out.bs.row[i] + j);
				for (size_t k = 0; k < nc; k++) {
					v[j*nc+k] = y;
				}
			}
			if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, nc)) return out;
		}

	} else if (value == "chess") {
		std::vector<double> a(nc);
		std::vector<double> b(nc);
		for (size_t i=0; i<nc; i++) {
			bool even = i%2 == 0;
			a[i] = even;
			b[i] = !even;
		}
		std::vector<double> v;
		for (size_t i = 0; i < out.bs.n; i++) {
			if ((out.bs.row[i]%2) == 0) {
				v = a;
				v.insert(v.end(), b.begin(), b.end());
			} else {
				v = b;
				v.insert(v.end(), b.begin(), b.end());
			}
			recycle(v, out.bs.nrows[i] * nc);
			if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, nc)) return out;
		}
	}

	out.writeStop();
	return(out);
}


SpatRaster SpatRaster::init(std::vector<double> values, SpatOptions &opt) {
	SpatRaster out = geometry();
 	if (!out.writeStart(opt)) { return out; }
	unsigned nc = ncol();
	unsigned nl = nc * nlyr();
	if (values.size() == 1) {
		std::vector<double> v(out.bs.nrows[0]*nc*nl, values[0]);
		for (size_t i = 0; i < out.bs.n; i++) {
			if ((i == (out.bs.n-1)) && (i > 0)) {
				// last block can be longer, it seems
				v.resize(out.bs.nrows[i]*nc*nl, values[0]);
			}
			if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, nc)) return out;
		}
	} else {
		int over = 0;
		for (size_t i = 0; i < out.bs.n; i++) {
			if (over > 0) {
				std::vector<double> newv(values.begin()+over, values.end());
				newv.insert(newv.end(), values.begin(), values.begin()+over);
				values = newv;
			}
			std::vector<double> v = values;
			recycle(v, out.bs.nrows[i]*nc);
			recycle(v, out.bs.nrows[i]*nc*nl);
			over = v.size() % values.size();
			if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, nc)) return out;
		}
	}
	out.writeStop();
	return(out);
}


SpatRaster SpatRaster::isnan(SpatOptions &opt) {
	SpatRaster out = geometry();
    if (!hasValues()) return out;
	if (!readStart()) {
		out.setError(getError());
		return(out);
	}

	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	for (size_t i=0; i<out.bs.n; i++) {
		std::vector<double> v = readBlock(out.bs, i);
		for (double &d : v) d = std::isnan(d);
		if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
	}
	readStop();
	out.writeStop();
	return(out);
}


SpatRaster SpatRaster::isnotnan(SpatOptions &opt) {
	SpatRaster out = geometry();
    if (!hasValues()) return out;

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	for (size_t i=0; i<out.bs.n; i++) {
		std::vector<double> v = readBlock(out.bs, i);
		for (double &d : v) d = ! std::isnan(d);
		if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
	}
	readStop();
	out.writeStop();
	return(out);
}


SpatRaster SpatRaster::isfinite(SpatOptions &opt) {
	SpatRaster out = geometry();
    if (!hasValues()) return out;

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	for (size_t i=0; i<out.bs.n; i++) {
		std::vector<double> v = readBlock(out.bs, i);
		for (double &d : v) d = std::isfinite(d);
		if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
	}
	readStop();
	out.writeStop();
	return(out);
}


SpatRaster SpatRaster::isinfinite(SpatOptions &opt) {
	SpatRaster out = geometry();
    if (!hasValues()) return out;

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	for (size_t i=0; i<out.bs.n; i++) {
		std::vector<double> v = readBlock(out.bs, i);
		for (double &d : v) d = std::isinf(d);
		if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
	}
	readStop();
	out.writeStop();
	return(out);
}


SpatRaster SpatRaster::rotate(bool left, SpatOptions &opt) {

	unsigned nc = ncol();
	unsigned nl = nlyr();
	unsigned hnc = (nc / 2);
	double addx = hnc * xres();
	if (left) {
		addx = -addx;
	}
	SpatRaster out = geometry(nlyr(), true);
	SpatExtent outext = out.getExtent();
	outext.xmin = outext.xmin + addx;
	outext.xmax = outext.xmax + addx;
	out.setExtent(outext, true);

	if (!hasValues()) return out;

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
 	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	std::vector<double> b;
	for (size_t i=0; i < out.bs.n; i++) {
		std::vector<double> a = readBlock(out.bs, i);
		for (size_t j=0; j < nl; j++) {
			for (size_t r=0; r < out.bs.nrows[i]; r++) {
				unsigned s1 = j * out.bs.nrows[i] * nc + r * nc;
				unsigned e1 = s1 + hnc;
				unsigned s2 = e1;
				unsigned e2 = s1 + nc;
				b.insert(b.end(), a.begin()+s2, a.begin()+e2);
				b.insert(b.end(), a.begin()+s1, a.begin()+e1);
			}
		}
		if (!out.writeValues(b, out.bs.row[i], nrow(), 0, ncol())) return out;
		b.resize(0);
	}
	out.writeStop();
	readStop();
	return(out);
}



bool SpatRaster::shared_basegeom(SpatRaster &x, double tol, bool test_overlap) {
	if (!compare_origin(x.origin(), tol)) return false;
	if (!about_equal(xres(), x.xres(), xres() * tol)) return false; 
	if (!about_equal(yres(), x.yres(), yres() * tol)) return false; 
	if (test_overlap) {
		SpatExtent e = x.getExtent();
		e.intersect(getExtent());
		if (!e.valid()) return false;
	}
	return true;
}





SpatRaster SpatRaster::cover(SpatRaster x, std::vector<double> values, SpatOptions &opt) {

	unsigned nl = std::max(nlyr(), x.nlyr());
	SpatRaster out = geometry(nl, true);

	bool rmatch = false;
	if (out.compare_geom(x, false, false, opt.get_tolerance(), true)) {
		rmatch = true;
	} else {
	//	if (!shared_basegeom(x, 0.1, true)) {
		out.setError("raster dimensions do not match");
		return(out);
	//	} else {
	//		out.msg.has_error = false;
	//		out.msg.error = "";
	//		SpatExtent e = getExtent();
	//		SpatExtent xe = x.getExtent();
	//		double prec = std::min(xres(), yres())/1000;
	//		if (!xe.compare(e, "<=", prec)) {
	//			SpatOptions xopt(opt);
	//			x = x.crop(e, "near", xopt);		
	//		}
	//	}
	}


	if (!x.hasValues()) {
		return *this;
	}
	if (!hasValues()) {
		if (rmatch) {
			return x.deepCopy();
		} else {
			SpatExtent e = getExtent();
			return x.extend(e, opt);
		}
	}

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
	if (!x.readStart()) {
		out.setError(x.getError());
		return(out);
	}

  	if (!out.writeStart(opt)) {
		readStop();
		x.readStop();
		return out;
	}
	if (values.size() == 1) {
		double value=values[0];
		for (size_t i = 0; i < out.bs.n; i++) {
			std::vector<double> v = readValues(out.bs.row[i], out.bs.nrows[i], 0, ncol());
			std::vector<double> m = x.readValues(out.bs.row[i], out.bs.nrows[i], 0, ncol());
			recycle(v, m);
			if (std::isnan(value)) {
				for (size_t i=0; i < v.size(); i++) {
					if (std::isnan(v[i])) {
						v[i] = m[i];
					}
				}
			} else {
				for (size_t i=0; i < v.size(); i++) {
					if (v[i] == value) {
						v[i] = m[i];
					}
				}
			}
			if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
		}
	} else {

		values = vunique(values);
		bool hasNA = false;
		for (int i = values.size()-1; i>=0; i--) {
			if (std::isnan(values[i])) {
				hasNA = true;
				values.erase(values.begin()+i);
			}
		}
	
		for (size_t i = 0; i < out.bs.n; i++) {
			std::vector<double> v = readValues(out.bs.row[i], out.bs.nrows[i], 0, ncol());
			std::vector<double> m = x.readValues(out.bs.row[i], out.bs.nrows[i], 0, ncol());
			recycle(v, m);
			for (size_t i=0; i < v.size(); i++) {
				if (hasNA) {
					if (std::isnan(v[i])) {
						v[i] = m[i];
						continue;
					}
				}
				for (size_t i=0; i<values.size(); i++) {
					if (v[i] == values[i]) {
						v[i] = m[i];
						continue;
					}
				}
			}
			if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
		}
	}

	out.writeStop();
	readStop();
	x.readStop();
	return(out);
}




SpatRaster SpatRaster::extend(SpatExtent e, SpatOptions &opt) {

	SpatRaster out = geometry(nlyr(), true);
	e = out.align(e, "near");
	SpatExtent extent = getExtent();
	e.unite(extent);

	out.setExtent(e, true);
	if (!hasValues() ) {
		if (opt.get_filename() != "") {
			out.addWarning("ignoring filename argument because there are no cell values");
		}
		return(out);
	}

	double tol = std::min(xres(), yres()) / 1000;
	if (extent.compare(e, "==", tol)) {
		// same extent
		if (opt.get_filename() != "") {
			out = writeRaster(opt);
		} else {
			out = deepCopy();
		}
		return out;
	}


	if (!readStart()) {
		out.setError(getError());
		return(out);
	}

 	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	out.fill(NAN);
	BlockSize bs = getBlockSize(opt);
	for (size_t i=0; i<bs.n; i++) {
        std::vector<double> v = readValues(bs.row[i], bs.nrows[i], 0, ncol());
        unsigned row1 = out.rowFromY(yFromRow(bs.row[i]));
        unsigned row2 = out.rowFromY(yFromRow(bs.row[i]+bs.nrows[i]-1));
        unsigned col1 = out.colFromX(xFromCol(0));
        unsigned col2 = out.colFromX(xFromCol(ncol()-1));
        if (!out.writeValues(v, row1, row2-row1+1, col1, col2-col1+1)) return out;
	}
	readStop();
	out.writeStop();
	return(out);
}



SpatRaster SpatRaster::crop(SpatExtent e, std::string snap, SpatOptions &opt) {

	SpatRaster out = geometry(nlyr(), true);

	if ( !e.valid() ) {
		out.setError("invalid extent");
		return out;
	} 
	e.intersect(out.getExtent());
	if ( !e.valid() ) {
		out.setError("extents do not overlap");
		return out;
	} 

	out.setExtent(e, true, snap);
	if (!hasValues() ) {
		if (opt.get_filename() != "") {
			out.addWarning("ignoring filename argument because there are no cell values");
		}
		return(out);
	}

	double xr = xres();
	double yr = yres();
	SpatExtent outext = out.getExtent();
	unsigned col1 = colFromX(outext.xmin + 0.5 * xr);
	unsigned col2 = colFromX(outext.xmax - 0.5 * xr);
	unsigned row1 = rowFromY(outext.ymax - 0.5 * yr);
	unsigned row2 = rowFromY(outext.ymin + 0.5 * yr);

	std::vector<bool> hw = hasWindow();
	bool haswin = hw[0];
	for (size_t i=1; i<nsrc(); i++) {
		haswin = (haswin | hw[i]);
	}

	if ((row1==0) && (row2==nrow()-1) && (col1==0) && (col2==ncol()-1) && (!haswin)) {
		// same extent
		if (opt.get_filename() != "") {
			out = writeRaster(opt);
		} else {
			out = deepCopy();
		}
		return out;
	}

	unsigned ncols = out.ncol();
	if (!readStart()) {
		out.setError(getError());
		return(out);
	}

	opt.ncopies = 2;
 	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	std::vector<double> v;
	for (size_t i = 0; i < out.bs.n; i++) {
		v = readValues(row1+out.bs.row[i], out.bs.nrows[i], col1, ncols);
		if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, out.ncol())) return out;
	}
	out.writeStop();
	readStop();

	return(out);
}

SpatRaster SpatRaster::flip(bool vertical, SpatOptions &opt) {

	SpatRaster out = geometry(nlyr(), true);
	if (!hasValues()) return out;
	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
 
	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	unsigned nc = ncol();
	unsigned nl = nlyr();

	if (vertical) {
		for (size_t i=0; i < out.bs.n; i++) {
			std::vector<double> b;
			size_t ii = out.bs.n - 1 - i;
			std::vector<double> a = readBlock(out.bs, ii);
			for (size_t j=0; j < out.nlyr(); j++) {
				size_t offset = j * out.bs.nrows[ii] * nc;
				for (size_t k=0; k < out.bs.nrows[ii]; k++) {
					unsigned start = offset + (out.bs.nrows[ii] - 1 - k) * nc;
					b.insert(b.end(), a.begin()+start, a.begin()+start+nc);
				}
			}
			if (!out.writeValues(b, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
		}
	} else {
		for (size_t i=0; i < out.bs.n; i++) {
			std::vector<double> b;
			std::vector<double> a = readBlock(out.bs, i);
			unsigned lyrrows = nl * out.bs.nrows[i];
			for (size_t j=0; j < lyrrows; j++) {
				unsigned start = j * nc;
				unsigned end = start + nc;
				std::vector<double> v(a.begin()+start, a.begin()+end);
				std::reverse(v.begin(), v.end());
				b.insert(b.end(), v.begin(), v.end());
			}
			if (!out.writeValues(b, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
			b.resize(0);
		}
	}
	out.writeStop();
	readStop();
	return(out);
}


SpatRaster SpatRaster::reverse(SpatOptions &opt) {

	SpatRaster out = geometry(nlyr(), true);
	if (!hasValues()) return out;
	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
 
	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	std::vector<double> b;
	unsigned nc = ncol();
	unsigned nl = nlyr();

	for (size_t i=0; i < out.bs.n; i++) {
		size_t ii = out.bs.n - 1 - i;
		std::vector<double> a = readBlock(out.bs, ii);
		unsigned lyrrows = nl * out.bs.nrows[ii];
		for (size_t j=0; j < lyrrows; j++) {
			unsigned start = (lyrrows - 1 - j) * nc;
			unsigned end = start + nc;
			std::vector<double> v(a.begin()+start, a.begin()+end);
			std::reverse(v.begin(), v.end());
			b.insert(b.end(), v.begin(), v.end());
		}
		if (!out.writeValues(b, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;
		b.resize(0);
	}

	out.writeStop();
	readStop();
	return(out);
}


SpatRaster SpatRaster::shift(double x, double y, SpatOptions &opt) {
	SpatRaster out = deepCopy();
	SpatExtent outext = out.getExtent();
	outext.xmin = outext.xmin + x;
	outext.xmax = outext.xmax + x;
	outext.ymin = outext.ymin + y;
	outext.ymax = outext.ymax + y;
	out.setExtent(outext, true);
	return out;
}

bool SpatRaster::compare_origin(std::vector<double> x, double tol) {
	std::vector<double> y = origin();
	if (!about_equal(x[0], y[0], xres() * tol)) return false; 
	if (!about_equal(x[1], y[1], yres() * tol)) return false; 
	return true;
}


/*
SpatRaster SpatRasterCollection::merge(SpatOptions &opt) {

	SpatRaster out;
	unsigned n = size();

	if (n == 0) {
		out.setError("empty collection");
		return(out);
	}
	if (n == 1) {
		out = ds[0].deepCopy();
		return(out);
	}

	bool any_hasvals = false;
	if (ds[0].hasValues()) any_hasvals = true;
	out = ds[0].geometry(ds[0].nlyr(), true);
	std::vector<double> orig = ds[0].origin(); 
	SpatExtent e = ds[0].getExtent();
	unsigned nl = ds[0].nlyr();
	for (size_t i=1; i<n; i++) {
									 //  lyrs, crs, warncrs, ext, rowcol, res
		if (!out.compare_geom(ds[i], false, false, false, false, false, true)) {
			return(out);
		}
		if (!out.compare_origin(ds[i].origin(), 0.1)) {
			out.setError("origin of SpatRaster " + std::to_string(i+1) + " does not match the previous SpatRaster(s)");
			return(out);		
		}
		e.unite(ds[i].getExtent());
		nl = std::max(nl, ds[i].nlyr());
		if (ds[i].hasValues()) any_hasvals = true;
	}
	out.setExtent(e, true);
	out = out.geometry(nl, true);
	if (!any_hasvals) return out;

 //   out.setResolution(xres(), yres());
 	if (!out.writeStart(opt)) { return out; }
	out.fill(NAN);

	for (size_t i=0; i<n; i++) {
		SpatRaster r = ds[i];
		if (!r.hasValues()) continue;
		BlockSize bs = r.getBlockSize(opt);
		if (!r.readStart()) {
			out.setError(r.getError());
			return(out);
		}

		for (size_t j=0; j<bs.n; j++) {
            std::vector<double> v = r.readValues(bs.row[j], bs.nrows[j], 0, r.ncol());
            unsigned row1  = out.rowFromY(r.yFromRow(bs.row[j]));
            unsigned row2  = out.rowFromY(r.yFromRow(bs.row[j]+bs.nrows[j]-1));
            unsigned col1  = out.colFromX(r.xFromCol(0));
            unsigned col2  = out.colFromX(r.xFromCol(r.ncol()-1));
			unsigned ncols = col2-col1+1;
			unsigned nrows = row2-row1+1;
			recycle(v, ncols * nrows * nl);
		
            if (!out.writeValues(v, row1, nrows, col1, ncols)) return out;
		}
		r.readStop();
	}

	out.writeStop();
	return(out);
}
*/




SpatRaster SpatRasterCollection::merge(SpatOptions &opt) {
	return mosaic("first", opt);
}




SpatRaster SpatRasterCollection::mosaic(std::string fun, SpatOptions &opt) {

	SpatRaster out;

	std::vector<std::string> f {"first", "sum", "mean", "median", "min", "max"};
	if (std::find(f.begin(), f.end(), fun) == f.end()) {
		out.setError("not a valid function");
		return out;
	}

	unsigned n = size();

	if (n == 0) {
		out.setError("empty collection");
		return(out);
	}
	if (n == 1) {
		out = ds[0].deepCopy();
		return(out);
	}

	std::vector<bool> hvals(n);
	hvals[0] = ds[0].hasValues();
	SpatExtent e = ds[0].getExtent();
	unsigned nl = ds[0].nlyr();
	std::vector<bool> resample(n, false);
	for (size_t i=1; i<n; i++) {
									//  lyrs, crs, warncrs, ext, rowcol, res
		if (!ds[0].compare_geom(ds[i], false, false, opt.get_tolerance(), false, false, false, true)) {
			out.setError(ds[0].msg.error);
			return(out);
		}		
		e.unite(ds[i].getExtent());
		hvals[i] = ds[i].hasValues();
		nl = std::max(nl, ds[i].nlyr());
	}
	out = ds[0].geometry(nl, false);
	out.setExtent(e, true, "");
	
	for (int i=(n-1); i>=0; i--) {
		if (!hvals[i]) {
			erase(i);
		}
	}
	
	n = size();
	if (size() == 0) {
		return out;
	}	

	SpatExtent eout = out.getExtent();
	double hyr = out.yres()/2;

	std::string warn = "";
	for (size_t i=0; i<n; i++) {
		SpatOptions topt(opt);
		if(!ds[i].shared_basegeom(out, 0.1, true)) {
			SpatRaster temp = out.crop(ds[i].getExtent(), "near", topt);
			std::vector<bool> hascats = ds[i].hasCategories();
			std::string method = hascats[0] ? "near" : "bilinear";
			ds[i] = ds[i].warper(temp, "", method, false, topt);
			if (ds[i].hasError()) {
				out.setError(ds[i].getError());
				return out;
			}
			warn = "rasters did not align and were resampled";
		}
	}
	if (warn != "") out.addWarning(warn);
	
 	if (!out.writeStart(opt)) { return out; }
	SpatOptions sopt(opt);
	sopt.progressbar = false;
	std::vector<double> v;
	for (size_t i=0; i < out.bs.n; i++) {
		eout.ymax = out.yFromRow(out.bs.row[i]) + hyr;
		eout.ymin = out.yFromRow(out.bs.row[i] + out.bs.nrows[i] - 1) - hyr;
		SpatRasterStack s;
		for (size_t j=0; j<n; j++) {
			e = ds[j].getExtent();
			e.intersect(eout);
			if ( e.valid_notequal() ) {
				SpatRaster r = ds[j].crop(eout, "near", sopt);
				//SpatExtent ec = r.getExtent();
				r = r.extend(eout, sopt);
				//SpatExtent ee = r.getExtent();
				if (!s.push_back(r, "", "", "", false)) {
					out.setError("internal error: " + s.getError());
					out.writeStop();
					return out;
				}
			}
		} 
		size_t ncls = out.bs.nrows[i] * out.ncol() * nl;
		if (s.size() > 0) {
			SpatRaster r = s.summary(fun, true, sopt);
			if (r.hasError()) {
				out.setError("internal error: " + r.getError());
				out.writeStop();
				return out;
			}
			if (!r.getValuesSource(0, v)) {
				out.setError("internal error: " + r.getError());
				out.writeStop();
				return out;			
			}
			recycle(v, ncls);
		} else {
			v = std::vector<double>(ncls, NAN); 
		}
		if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, out.ncol())) return out;
	}
	out.writeStop();
	return(out);
}


void notisnan(const std::vector<double> &x, double &n) {
	for (size_t i=0; i<x.size(); i++) {
		n += !std::isnan(x[i]);
	}
}



void do_stats(std::vector<double> &v, std::string fun, bool narm, double &stat, double &stat2,double &n, size_t step) {
	if (v.size() == 0) return;
	if (fun == "sum") {
		if (narm && (step > 0)) {
			v.push_back(stat);
		} 
		stat = vsum(v, narm);
	} else if (fun == "mean") {
		if (narm) {
			notisnan(v, n);
			if (step > 0) {
				v.push_back(stat);
			}
		} else {
			n += v.size();
		}
		stat = vsum(v, narm);
	} else if (fun == "rms") {
		if (narm) {
			notisnan(v, n);
		} else {
			n += v.size();
		}
		double s = vsum2(v, narm);
		if (step > 1) {
			std::vector<double> ss = {stat, s};
			stat = vsum(ss, narm);
		} else {
			stat = s;
		}		
	} else if (fun == "min") {
		double s = vmin(v, narm);
		if (step > 0) {
			std::vector<double> ss = {stat, s};
			stat = vmin(ss, narm);
		} else {
			stat = s;		
		}
	} else if (fun == "max") {
		double s = vmax(v, narm);
		if (step > 0) {
			std::vector<double> ss = {stat, s};
			stat = vmax(ss, narm);
		} else {
			stat = s;		
		}
	} else if (fun == "range") {
		double sn = vmin(v, narm);
		double sx = vmax(v, narm);
		if (step > 0) {
			std::vector<double> ss1 = {stat, sn};
			stat = vmin(ss1, narm);
			std::vector<double> ss2 = {stat2, sx};
			stat2 = vmax(ss2, narm);
		} else {
			stat = sn;		
			stat2 = sx;		
		}
	} else if (fun == "sd") {
		if (narm) {
			notisnan(v, n);
		} else {
			n += v.size();
		}
		double s1 = vsum(v, narm);
		double s2 = vsum2(v, narm);
		if (step > 1) {
			std::vector<double> ss1 = {stat, s1};
			stat = vsum(ss1, narm);
			std::vector<double> ss2 = {stat2, s2};
			stat2 = vsum(ss2, narm);
		} else {
			stat = s1;
			stat2 = s2;
		}		
	}
}


SpatDataFrame SpatRaster::global(std::string fun, bool narm, SpatOptions &opt) {

	SpatDataFrame out;
	std::vector<std::string> f {"sum", "mean", "min", "max", "range", "rms", "sd", "sdpop"};
	if (std::find(f.begin(), f.end(), fun) == f.end()) {
		out.setError("not a valid function");
		return(out);
	}

	if (!hasValues()) {
		out.setError("SpatRaster has no values");
		return(out);
	}

	std::string sdfun = fun;
	if ((fun == "sdpop") || (fun == "sd")) {
		fun = "sd";
	}
	std::vector<double> stats(nlyr());
	std::vector<double> stats2(nlyr());
	
	std::vector<double> n(nlyr());
	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
	BlockSize bs = getBlockSize(opt);
	for (size_t i=0; i<bs.n; i++) {
		std::vector<double> v = readValues(bs.row[i], bs.nrows[i], 0, ncol());
		unsigned off = bs.nrows[i] * ncol() ;
		for (size_t lyr=0; lyr<nlyr(); lyr++) {
			unsigned offset = lyr * off;
			std::vector<double> vv = { v.begin()+offset, v.begin()+offset+off };
			do_stats(vv, fun, narm, stats[lyr], stats2[lyr], n[lyr], i);
		}
	}
	readStop();


	if (fun=="mean") {
		for (size_t lyr=0; lyr<nlyr(); lyr++) {
			if (n[lyr] > 0) {
				stats[lyr] = stats[lyr] / n[lyr];
			} else {
				stats[lyr] = NAN;
			}
		}
	} else if (fun=="rms") {
		// rms = sqrt(sum(x^2)/(n-1))
		for (size_t lyr=0; lyr<nlyr(); lyr++) {
			if (n[lyr] > 0) {
				stats[lyr] = sqrt(stats[lyr] / (n[lyr]-1));
			} else {
				stats[lyr] = NAN;
			}
		}
	} else if (fun == "sd") {
		for (size_t lyr=0; lyr<nlyr(); lyr++) {
			if (n[lyr] > 0) {
				double mn = stats[lyr] / n[lyr];
				double mnsq = mn * mn;
				double mnsumsq = stats2[lyr] / n[lyr];
				if (sdfun == "sdpop") {
					stats[lyr] = sqrt(mnsumsq - mnsq);
				} else {
					stats[lyr] = sqrt((mnsumsq - mnsq) * n[lyr]/(n[lyr]-1));
				}

			} else {
				stats[lyr] = NAN;
			}
		}
	}
	out.add_column(stats, fun);
	if (fun=="range") {
		out.add_column(stats2, "max");
	}
	return(out);
}



SpatDataFrame SpatRaster::global_weighted_mean(SpatRaster &weights, std::string fun, bool narm, SpatOptions &opt) {

	SpatDataFrame out;

	std::vector<std::string> f {"sum", "mean"};
	if (std::find(f.begin(), f.end(), fun) == f.end()) {
		out.setError("not a valid function");
		return(out);
	}

	if (!hasValues()) {
		out.setError("SpatRaster has no values");
		return(out);
	}

	if (weights.nlyr() != 1) {
		out.setError("The weights raster must have 1 layer");
		return(out);
	}
	if (!compare_geom(weights, false, false, opt.get_tolerance(), true)) {
		out.setError( msg.getError() );
		return(out);
	}

	std::vector<double> stats(nlyr());
	double stats2 = 0;
	std::vector<double> n(nlyr());
	std::vector<double> w(nlyr());
	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
	if (!weights.readStart()) {
		out.setError(weights.getError());
		return(out);
	}

	BlockSize bs = getBlockSize(opt);
	for (size_t i=0; i<bs.n; i++) {
		std::vector<double> v = readValues(bs.row[i], bs.nrows[i], 0, ncol());
		std::vector<double> wv = weights.readValues(bs.row[i], bs.nrows[i], 0, ncol());
	
		unsigned off = bs.nrows[i] * ncol() ;
		for (size_t lyr=0; lyr<nlyr(); lyr++) {
			double wsum = 0;
			unsigned offset = lyr * off;
			std::vector<double> vv(v.begin()+offset,  v.begin()+offset+off);
			for (size_t j=0; j<vv.size(); j++) {
				if (!std::isnan(vv[j]) && !std::isnan(wv[j])) {
					vv[j] *= wv[j];
					wsum += wv[j];
				} else {
					vv[j] = NAN;
				}
			}
			do_stats(vv, fun, narm, stats[lyr], stats2, n[lyr], i);
			w[lyr] += wsum; 
		}
	}
	readStop();
	weights.readStop();

	if (fun=="mean") {
		for (size_t lyr=0; lyr<nlyr(); lyr++) {
			if (n[lyr] > 0 && w[lyr] != 0) {
				stats[lyr] /= w[lyr];
			} else {
				stats[lyr] = NAN;
			}
		}
		out.add_column(stats, "weighted_mean");
	} else {
		out.add_column(stats, "weighted_sum");
	}
	
	return(out);
}


SpatRaster SpatRaster::scale(std::vector<double> center, bool docenter, std::vector<double> scale, bool doscale, SpatOptions &opt) {
	SpatRaster out;
	SpatOptions opts(opt);
	SpatDataFrame df;
	if (docenter) {
		if (center.size() == 0) {
			df = global("mean", true, opts);
			center = df.getD(0);
		}
		if (doscale) {
			out = arith(center, "-", false, opts);
		} else {
			out = arith(center, "-", false, opt);		
		}
	} 
	if (doscale) {
		if (scale.size() == 0) {
			// divide by sd if centered, and the root mean square otherwise.
			// rms = sqrt(sum(x^2)/(n-1)); if centered rms == sd
			if (docenter) {
				df = out.global("rms", true, opts);
			} else {
				df = global("rms", true, opts);
			}
			scale = df.getD(0);		
		}
		if (docenter) {
			out = out.arith(scale, "/", false, opt);
		} else {
			out = arith(scale, "/", false, opt);
		}
	}
	return out;
}


void reclass_vector(std::vector<double> &v, std::vector<std::vector<double>> rcl, unsigned doright, bool lowest, bool othNA) {

	size_t nc = rcl.size(); // should be 2 or 3
	if (nc == 2) {
		doright = 3; // should be 2?
	}
	bool right = false;
	bool leftright = false;
	if (doright > 1) {
		leftright = true;
	} else if (doright) {
		right = true;
	}

//	bool hasNA = false;
	double NAval = NAN;

	size_t n = v.size();
	unsigned nr = rcl[0].size();

	if (nc == 1) {
		std::vector<double> rc = rcl[0];
		std::sort(rc.begin(), rc.end());
		if (right) {   // interval closed at left and right
			if (lowest)	{
				for (size_t i=0; i<n; i++) {
					if (std::isnan(v[i])) {
						v[i] = NAval;
					} else if ((v[i] < rc[0]) | (v[i] > rc[nr-1])) {
						v[i] = NAval;
					} else {
						for (size_t j=1; j<nr; j++) {
							if (v[i] <= rc[j]) {
								v[i] = j-1;
								break;
							}
						}
					}
				}
			} else {
				for (size_t i=0; i<n; i++) {
					if (std::isnan(v[i])) {
						v[i] = NAval;
					} else if ((v[i] <= rc[0]) | (v[i] > rc[nr-1])) {
						v[i] = NAval;
					} else {
						for (size_t j=1; j<nr; j++) {
							if (v[i] <= rc[j]) {
								v[i] = j-1;
								break;
							}
						}
					}
				}
			}
		} else {
			if (lowest)	{
				for (size_t i=0; i<n; i++) {
					if (std::isnan(v[i])) {
						v[i] = NAval;
					} else if ((v[i] < rc[0]) | (v[i] > rc[nr-1])) {
						v[i] = NAval;
					} else if (v[i] == rc[nr-1]) {
						v[i] = nr-1;
					} else {
						for (size_t j=1; j<nr; j++) {
							if (v[i] < rc[j]) {
								v[i] = j-1;
								break;
							}
						}
					}
				}
			} else {
				for (size_t i=0; i<n; i++) {
					if (std::isnan(v[i])) {
						v[i] = NAval;
					} else if ((v[i] < rc[0]) | (v[i] >= rc[nr-1])) {
						v[i] = NAval;
					} else {
						for (size_t j=1; j<nr; j++) {
							if (v[i] < rc[j]) {
								v[i] = j-1;
								break;
							}
						}
					}
				}
			}
		}

	// "is - becomes"
	} else if (nc == 2) {

		bool hasNAN = false;
		double replaceNAN = NAval;
		for (size_t j=0; j<nr; j++) {
			if (std::isnan(rcl[0][j])) {
				hasNAN = true;
				replaceNAN = rcl[1][j];
			}
		} 
		for (size_t i=0; i<n; i++) {
			if (std::isnan(v[i])) {
				if (hasNAN) {
					v[i] = replaceNAN;
				} else {
					v[i] = NAval;
				}
			} else {
				bool found = false;
				for (size_t j=0; j<nr; j++) {
					if (v[i] == rcl[0][j]) {
						v[i] = rcl[1][j];
						found = true;
						break;
					}
				}
				if ((othNA) & (!found)) {
					v[i] = NAval;
				}
			}
		}

	// "from - to - becomes"
	} else {
	
		bool hasNAN = false;
		double replaceNAN = NAval;
		for (size_t j=0; j<nr; j++) {
			if (std::isnan(rcl[0][j]) || std::isnan(rcl[1][j])) {
				hasNAN = true;
				replaceNAN = rcl[2][j];
			}
		} 
	
		if (leftright) {   // interval closed at left and right

			for (size_t i=0; i<n; i++) {
				if (std::isnan(v[i])) {
					if (hasNAN) {
						v[i] = replaceNAN;
					} else {
						v[i] = NAval;
					}
				} else {
					bool found = false;
					for (size_t j=0; j<nr; j++) {
						if ((v[i] >= rcl[0][j]) & (v[i] <= rcl[1][j])) {
							v[i] = rcl[2][j];
							found = true;
							break;
						}
					}
					if ((othNA) & (!found))  {
						v[i] = NAval;
					}			
				}
			}
		} else if (right) {  // interval closed at right
				if (lowest) {  // include lowest value (left) of interval

				double lowval = rcl[0][0];
				double lowres = rcl[2][0];
				for (size_t i=1; i<nr; i++) {
					if (rcl[0][i] < lowval) {
						lowval = rcl[0][i];
						lowres = rcl[2][i];
					}
				}

				for (size_t i=0; i<n; i++) {
					if (std::isnan(v[i])) {
						if (hasNAN) {
							v[i] = replaceNAN;
						} else {
							v[i] = NAval;
						}
					} else if (v[i] == lowval) {
						v[i] = lowres;
					} else {
						bool found = false;
						for (size_t j=0; j<nr; j++) {
							if ((v[i] > rcl[0][j]) & (v[i] <= rcl[1][j])) {
								v[i] = rcl[2][j];
								found = true;
								break;
							}
						}
						if  ((othNA) & (!found))  {
							v[i] = NAval;
						}
					}
				}

			} else { // !dolowest
					for (size_t i=0; i<n; i++) {
					if (std::isnan(v[i])) {
						if (hasNAN) {
							v[i] = replaceNAN;
						} else {
							v[i] = NAval;
						}
					} else {
						bool found = false;
						for (size_t j=0; j<nr; j++) {
							if ((v[i] > rcl[0][j]) & (v[i] <= rcl[1][j])) {
								v[i] = rcl[2][j];
								found = true;
								break;
							}
						}
						if  ((othNA) & (!found))  {
							v[i] = NAval;
						}
					}
				}
			}

		} else { // !doright

			if (lowest) { // which here means highest because right=FALSE

				double lowval = rcl[1][0];
				double lowres = rcl[2][0];
				for (size_t i=0; i<nr; i++) {
					if (rcl[1][i] > lowval) {
						lowval = rcl[1][i];
						lowres = rcl[2][i];
					}
				}

				for (size_t i=0; i<n; i++) {
					if (std::isnan(v[i])) {
						if (hasNAN) {
							v[i] = replaceNAN;
						} else {
							v[i] = NAval;
						}
					} else if (v[i] == lowval) {
						v[i] = lowres;
					} else {
						bool found = false;
						for (size_t j=0; j<nr; j++) {
							if ((v[i] >= rcl[0][j]) & (v[i] < rcl[1][j])) {
								v[i] = rcl[2][j];
								found = true;
								break;
							}
						}
						if  ((othNA) & (!found))  {
							v[i] = NAval;
						}
					}
				}

			} else { //!dolowest

				for (size_t i=0; i<n; i++) {
					if (std::isnan(v[i])) {
						if (hasNAN) {
							v[i] = replaceNAN;
						} else {
							v[i] = NAval;
						}
					} else {
						bool found = false;
						for (size_t j=0; j<nr; j++) {
							if ((v[i] >= rcl[0][j]) & (v[i] < rcl[1][j])) {
								v[i] = rcl[2][j];
								found = true;
								break;
							}
						}
						if  ((othNA) & (!found))  {
							v[i] = NAval;
						}
					}
				}
			}
		}
	}
}


SpatRaster SpatRaster::replaceValues(std::vector<double> from, std::vector<double> to, long nl, SpatOptions &opt) {

	SpatRaster out = geometry(nl);
	bool multi = false;
	if (nl > 1) {
		if (nlyr() > 1) {
			out.setError("cannot create layer-varying replacement with multi-layer input");
			return out;
		}	
		multi = true;
	}

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
  	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}

	if (multi) {
		size_t tosz = to.size() / nl;
		size_t nlyr = out.nlyr();
		for (size_t i = 0; i < out.bs.n; i++) {
			std::vector<double> v = readBlock(out.bs, i);
			size_t vs = v.size();
			v.reserve(vs * nlyr);
			for (size_t lyr = 1; lyr < nlyr; lyr++) {
				v.insert(v.end(), v.begin(), v.begin()+vs);
			}
			for (size_t lyr = 0; lyr < nlyr; lyr++) {
				std::vector<double> tolyr(to.begin()+lyr*tosz, to.begin()+(lyr+1)*tosz);
				recycle(tolyr, from);
				size_t offset = lyr*vs;
				for (size_t j=0; j< from.size(); j++) {
					if (std::isnan(from[j])) {
						for (size_t k=offset; k<(offset+vs); k++) {
							v[k] = std::isnan(v[k]) ? tolyr[j] : v[k];
						}
					} else {
						std::replace(v.begin()+offset, v.begin()+(offset+vs), from[j], tolyr[j]);
					}
				}
			}
			if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;	
		}
	} else {
		recycle(to, from);
		for (size_t i = 0; i < out.bs.n; i++) {
			std::vector<double> v = readBlock(out.bs, i);
			for (size_t j=0; j< from.size(); j++) {
				if (std::isnan(from[j])) {
					for (double &d : v) d = std::isnan(d) ? to[j] : d;
				} else {
					std::replace(v.begin(), v.end(), from[j], to[j]);
				}
			}
			if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;	
		}
	}
	readStop();
	out.writeStop();
	return(out);
}



SpatRaster SpatRaster::reclassify(std::vector<std::vector<double>> rcl, unsigned right, bool lowest, bool othersNA, bool bylayer, SpatOptions &opt) {

	SpatRaster out = geometry();
	size_t nc = rcl.size();
	size_t nr = rcl[0].size();
	size_t nl = nlyr();
	if (nl == 1) bylayer = false;
	size_t maxnc = 3 + nl * bylayer;
	size_t rcldim = nc;

	if (bylayer) {
		if (((nc != maxnc) && (nc != (maxnc-1))) || nr < 1) {
			out.setError("reclass matrix is not correct. Should be nlyr(x) plus 1 or 2");
			return out;
		}		
		rcldim = nc - (nl-1);
	} else {
		if (nc < 1 || nc > 3 || nr < 1) {
			out.setError("matrix must have 1, 2 or 3 columns, and at least one row");
			return out;
		}
	}
	
	if (nc == 1) {
		if (nr == 1) {
			int breaks = rcl[0][0];
			if (breaks < 2) {
				out.setError("cannot classify with a single number that is smaller than 2");
				return out;
			}
			std::vector<bool> hr = hasRange();
			bool hasR = true;
			for (size_t i=0; i<hr.size(); i++) {
				if (!hr[i]) hasR = false;
			}
			if (!hasR) setRange();
			std::vector<double> mn = range_min();
			std::vector<double> mx = range_max();
			double mnv = vmin(mn, true);
			double mxv = vmax(mx, true);
			rcl[0] = seq_steps(mnv, mxv, breaks);
		}
		
		if (rcl[0].size() < 256) {
			std::vector<std::string> s;
			for (size_t i=1; i<rcl[0].size(); i++) {
				s.push_back(double_to_string(rcl[0][i-1]) + " - " + double_to_string(rcl[0][i]));
			}
			for (size_t i=0; i<out.nlyr(); i++) {
				out.setLabels(i, s);
			}
		}
		nr = rcl[0].size();
	}
	for (size_t i=0; i<nc; i++) {
		if (rcl[i].size() != nr) {
			out.setError("matrix is not rectangular");
			return out;
		}
	}
	if (rcldim == 3) {
		for (size_t i=0; i<nr; i++) {
			if (rcl[0][i] > rcl[1][i]) {
				out.setError("'from' larger than 'to': (" + std::to_string(rcl[0][i]) + " - " + std::to_string(rcl[1][i]) +")");
				return out;
			}
		}
	}

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}

  	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	
	if (bylayer) {
		std::vector<std::vector<double>> lyrrcl(rcldim+1);
		for (size_t i=0; i<rcldim; i++) {
			lyrrcl[i] = rcl[i];
		}
		for (size_t i = 0; i < out.bs.n; i++) {
			unsigned off = bs.nrows[i] * ncol() ;
			std::vector<double> v = readBlock(out.bs, i);
			for (size_t lyr = 0; lyr < nl; lyr++) {
				unsigned offset = lyr * off;
				lyrrcl[rcldim] = rcl[rcldim+lyr];
				std::vector<double> vx(v.begin()+offset, v.begin()+offset+off);
				reclass_vector(vx, lyrrcl, right, lowest, othersNA);
				std::copy(vx.begin(), vx.end(), v.begin()+offset);
			}
			if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;	
		}
	} else {
		for (size_t i = 0; i < out.bs.n; i++) {
			std::vector<double> v = readBlock(out.bs, i);
			reclass_vector(v, rcl, right, lowest, othersNA);
			if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, ncol())) return out;	
		}
	}
	
	readStop();
	out.writeStop();
	return(out);

}


SpatRaster SpatRaster::reclassify(std::vector<double> rcl, unsigned nc, unsigned right, bool lowest, bool othersNA, bool bylayer, SpatOptions &opt) {

	SpatRaster out;
	if ((rcl.size() % nc) != 0) {
		out.setError("incorrect length of reclassify matrix");
		return(out);
	}
	size_t maxnc = 3 + bylayer * (nlyr() - 1);
	unsigned nr = rcl.size() / nc;
	if (nc > maxnc) {
		out.setError("incorrect number of columns in reclassify matrix");
		return(out);
	}
	std::vector< std::vector<double>> rc(nc);
	
	for (size_t i=0; i<nc; i++) {
		rc[i] = std::vector<double>(rcl.begin()+(i*nr), rcl.begin()+(i+1)*nr);
	}

	out = reclassify(rc, right, lowest, othersNA, bylayer, opt);
	return out;
}



std::vector<std::vector<double>> clump_getRCL(std::vector<std::vector<size_t>> rcl, size_t n) {
	std::vector<std::vector<size_t>> rcl2(rcl[0].size());
	for (size_t i=0; i<rcl[0].size(); i++) {
		rcl2[i].push_back(rcl[0][i]);
		rcl2[i].push_back(rcl[1][i]);
	}
    std::sort(rcl2.begin(), rcl2.end());
    rcl2.erase(std::unique(rcl2.begin(), rcl2.end()), rcl2.end());
	std::vector<std::vector<double>> out(2);
	for (size_t i=0; i<rcl2.size(); i++) {
		out[0].push_back(rcl2[i][1]);
		out[1].push_back(rcl2[i][0]);
	}
	// from - to 
	// 3 - 1
	// 4 - 3
    // becomes
    // 3 - 1
    // 4 - 1
	for (size_t i=1; i<out[0].size(); i++) {
		for (size_t j=0; j<i; j++) {
			if (out[0][i] == out[1][j]) {
				out[1][j] = out[0][i];
			}
		}
	}

	std::vector<double> lost = out[0];
	lost.push_back(n);
	size_t sub = 0;
	for (size_t i=0; i<lost.size(); i++) {
		sub++;
		for (size_t j=lost[i]+1; j<lost[i+1]; j++) {
			out[0].push_back(j);
			out[1].push_back(j-sub);
		}
	}
	return out;
}


void clump_replace(std::vector<double> &v, size_t n, const std::vector<double>& d, size_t cstart, std::vector<std::vector<size_t>>& rcl) {
	for (size_t i=0; i<n; i++) {
		for (size_t j=1; j<d.size(); j++) {
			if (v[i] == d[j]) {
				v[i] = d[0];
			}
		}
	}
	if (d[0] < cstart) {
		for (size_t j=1; j<d.size(); j++) {
			rcl[0].push_back(d[0]);
			rcl[1].push_back(d[j]);
		}
	}
}


void clump_test(std::vector<double> &d) {
	d.erase(std::remove_if(d.begin(), d.end(),
		[](const double& v) { return std::isnan(v); }), d.end());
	std::sort(d.begin(), d.end());
	d.erase(std::unique(d.begin(), d.end()), d.end());
}

void broom_clumps(std::vector<double> &v, std::vector<double>& above, const size_t &dirs, size_t &ncps, const size_t &nr, const size_t &nc, std::vector<std::vector<size_t>> &rcl) {

	size_t nstart = ncps;

	bool d4 = dirs == 4;

	if ( !std::isnan(v[0]) ) { //first cell, no cell left of it
		if (std::isnan(above[0])) {
			v[0] = ncps;
			ncps++;
		} else {
			v[0] = above[0];
		}
	}

	for (size_t i=1; i<nc; i++) { //first row, no row above it, use "above"
		if (!std::isnan(v[i])) {
			std::vector<double> d;
			if (d4) {
				d = {above[i], v[i-1]} ;
			} else {
				d = {above[i], above[i-1], v[i-1]} ;
			}
			clump_test(d);
			if (d.size() > 0) {
				v[i] = d[0];
				if (d.size() > 1) {
					clump_replace(v, i, d, nstart, rcl);
				}
			} else {
				v[i] = ncps;
				ncps++;
			}
		}
	}


	for (size_t r=1; r<nr; r++) { //other rows
		size_t i=r*nc;
		if (!std::isnan(v[i])) { // first cell
			if (std::isnan(v[i-nc])) {
				v[i] = ncps;
				ncps++;
			} else {
				v[i] = v[i-nc];
			}
		}
		for (size_t i=r*nc+1; i<((r+1)*nc); i++) { // other cells
			if (!std::isnan(v[i])) {
				std::vector<double> d;
				if (d4) {
					d = {v[i-nc], v[i-1]} ;
				} else {
					d = {v[i-nc], v[i-nc-1], v[i-1]} ;
				}
				clump_test(d);
				if (d.size() > 0) {
					v[i] = d[0];
					if (d.size() > 1) {
						clump_replace(v, i, d, nstart, rcl);
					}
				} else {
					v[i] = ncps;
					ncps++;
				}
			}
		}
	}
	size_t off = (nr-1) * nc;
	above = std::vector<double>(v.begin()+off, v.end());
}



SpatRaster SpatRaster::clumps(int directions, bool zeroAsNA, SpatOptions &opt) {

	SpatRaster out = geometry(1);
	if (nlyr() > 1) {
		SpatOptions ops(opt);
		std::string filename = opt.get_filename();
		ops.set_filenames({""});
		for (size_t i=0; i<nlyr(); i++) {
			std::vector<unsigned> lyr = {(unsigned)i};
			SpatRaster x = subset(lyr, ops);
			x = x.clumps(directions, zeroAsNA, ops);
			out.addSource(x);
		}
		if (filename != "") {
			out = out.writeRaster(opt);
		}
		return out;
	}

	if (!(directions == 4 || directions == 8)) {
		out.setError("directions must be 4 or 8");
		return out;
	}
	if (!hasValues()) {
		out.setError("cannot compute clumps for a raster with no values");
		return out;
	}

	std::vector<size_t> dim = {nrow(), ncol()};

	std::string tempfile = "";
    std::vector<double> d, v, vv;
	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
	std::string filename = opt.get_filename();
	if (filename != "") {
		bool overwrite = opt.get_overwrite();
		std::string errmsg;
		if (!can_write(filename, overwrite, errmsg)) {
			out.setError(errmsg + " (" + filename +")");
			return(out);
		}
	}

	opt.set_filenames({""});
 	if (!out.writeStart(opt)) { return out; }
	size_t nc = ncol();
	size_t ncps = 1;
	std::vector<double> above(nc, NAN);
	std::vector<std::vector<size_t>> rcl(2);
	for (size_t i = 0; i < out.bs.n; i++) {
        v = readBlock(out.bs, i);
		if (zeroAsNA) {
			std::replace(v.begin(), v.end(), 0.0, (double)NAN);
		}
        broom_clumps(v, above, directions, ncps, out.bs.nrows[i], nc, rcl);
		if (!out.writeValues(v, out.bs.row[i], out.bs.nrows[i], 0, nc)) return out;
	}
	out.writeStop();
	readStop();

	opt.set_filenames({filename});
	if (rcl[0].size() > 0) {
		std::vector<std::vector<double>> rc = clump_getRCL(rcl, ncps);
		out = out.reclassify(rc, 3, true, false, false, opt);
	} else if (filename != "") {
		out = out.writeRaster(opt);
	}
	return out;
}



