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

#include "spatRaster.h"
#include "distance.h"
#include <limits>
#include <cmath>


std::vector<double> shortDistPoints(const std::vector<double> &x, const std::vector<double> &y, const std::vector<double> &px, const std::vector<double> &py, const bool& lonlat, double lindist) {
	std::vector<double> out;
	if (lonlat) {
		double a = 6378137.0;
		double f = 1/298.257223563;
		out = distanceToNearest_lonlat(x, y, px, py, a, f);
	} else {
		out = distanceToNearest_plane(x, y, px, py);
		if (lindist != 1) {
			for (double &d : out) d *= lindist;
		}
	}
	return out;
}



SpatRaster SpatRaster::distance(SpatVector p, SpatOptions &opt) {

	SpatRaster out = geometry();
	if (source[0].srs.wkt == "") {
		out.setError("CRS not defined");
		return(out);
	}

	double m = source[0].srs.to_meter();
	m = std::isnan(m) ? 1 : m;

	std::string gtype = p.type();
	if (gtype != "points") {
		SpatOptions ops;
		std::vector<double> feats(p.size(), 1) ;
		SpatRaster x = rasterize(p, "", feats, {""}, NAN, false, false, false, false, ops);
		if (gtype == "polygons") {
			std::string etype = "inner";
			x = x.edges(false, etype, 8, ops);
		}
		p = x.as_points(false, true, opt);
	}


	if (p.size() == 0) {
		out.setError("no overlap between the vector and raster data");
		return(out);
	}
	
	bool lonlat = is_geographic(); // m == 0
	unsigned nc = ncol();
	if (!readStart()) {
		out.setError(getError());
		return(out);
	}

 	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	for (size_t i = 0; i < out.bs.n; i++) {
		double s = out.bs.row[i] * nc;
		std::vector<double> cells(out.bs.nrows[i] * nc) ;
		std::iota (cells.begin(), cells.end(), s);
		std::vector<std::vector<double>> xy = xyFromCell(cells);
		std::vector<std::vector<double>> pxy = p.coordinates();
		std::vector<double> d = shortDistPoints(xy[0], xy[1], pxy[0], pxy[1], lonlat, m);
		if (!out.writeValues(d, out.bs.row[i], out.bs.nrows[i], 0, nc)) return out;
	}
	out.writeStop();
	readStop();
	return(out);
}


SpatRaster SpatRaster::distance(SpatOptions &opt) {
	SpatRaster out = geometry();
	SpatOptions ops;
	if (nlyr() > 1) {
		out.addWarning("distance computations can only be done for one layer at a time --- to be improved");
		std::vector<unsigned> lyr = {0};
		subset(lyr, ops);
	}
	if (!hasValues()) {
		out.setError("SpatRaster has no values");
		return out;
	}

	std::string etype = "inner";
	SpatRaster e = edges(false, etype, 8, ops);
	SpatVector p = e.as_points(false, true, opt);
	out = out.distance(p, opt);
	return out;
}




std::vector<double> SpatVector::distance(bool sequential) {
	std::vector<double> d;
	if (srs.is_empty()) {
		setError("crs not defined");
		return(d);
	}
	double m = srs.to_meter();
	m = std::isnan(m) ? 1 : m;
	bool lonlat = is_geographic(); // m == 0
	
//	if ((!lonlat) || (gtype != "points")) {
	std::string gtype = type();
	if (gtype != "points") {
		d = geos_distance(sequential);
		if ((!lonlat) && (m != 1)) {
			for (double &i : d) i *= m;
		}
		return d;
	} else {
		if (sequential) {
			std::vector<std::vector<double>> p = coordinates();
			size_t n = p[0].size();
			d.reserve(n);
			d.push_back(0);
			n -= 1;
			if (lonlat) {
				double a = 6378137.0;
				double f = 1/298.257223563;	
				for (size_t i=0; i<n; i++) {
					d.push_back(
						distance_lonlat(p[0][i], p[1][i], p[0][i+1], p[1][i+1], a, f)
					);
				}
			} else {
				for (size_t i=0; i<n; i++) {
					d.push_back(
						distance_plane(p[0][i], p[1][i], p[0][i+1], p[1][i+1]) * m
					);
				}
			}
			
		} else {
			size_t s = size();
			size_t n = ((s-1) * s)/2;
			d.reserve(n);
			std::vector<std::vector<double>> p = coordinates();
			if (lonlat) {
				double a = 6378137.0;
				double f = 1/298.257223563;	
				for (size_t i=0; i<(s-1); i++) {
					for (size_t j=(i+1); j<s; j++) {
						d.push_back(
							distance_lonlat(p[0][i], p[1][i], p[0][j], p[1][j], a, f)
						);
					}
				}
			} else {
				for (size_t i=0; i<(s-1); i++) {
					for (size_t j=(i+1); j<s; j++) {
						d.push_back(
							distance_plane(p[0][i], p[1][i], p[0][j], p[1][j]) * m
						);
					}
				}
			}
		}
	}
	
	return d;
}


std::vector<double>  SpatVector::distance(SpatVector x, bool pairwise) {

	std::vector<double> d;

	if (srs.is_empty() || x.srs.is_empty()) {
		setError("SRS not defined");
		return(d);
	}
	if (! srs.is_same(x.srs, false) ) {
		setError("SRS do not match");
		return(d);
	}
	double m = srs.to_meter();
	m = std::isnan(m) ? 1 : m;
	bool lonlat = is_geographic();

	std::string gtype = type();
	std::string xtype = x.type();
	if ((!lonlat) || (gtype != "points") || (xtype != "points")) {
		d = geos_distance(x, pairwise);
		if ((!lonlat) && (m != 1)) {
			for (double &i : d) i *= m;
		}
		return d;
	}

	size_t s = size();
	size_t sx = x.size();
	if (s != sx) {
		pairwise = false;
	}
	size_t n = pairwise ? s : s*sx;
	d.resize(n);	
	std::vector<std::vector<double>> p = coordinates();
	std::vector<std::vector<double>> px = x.coordinates();

	if (pairwise) {
		if (lonlat) {
			double a = 6378137.0;
			double f = 1/298.257223563;	
			for (size_t i = 0; i < s; i++) {
				d[i] = distance_lonlat(p[0][i], p[1][i], px[0][i], px[1][i], a, f);
			}
		} else { // not reached
			for (size_t i = 0; i < s; i++) {
				d[i] = distance_plane(p[0][i], p[1][i], px[0][i], px[1][i]) * m;
			}		
		} 
	} else {	
		if (lonlat) {
			double a = 6378137.0;
			double f = 1/298.257223563;	
			for (size_t i=0; i<s; i++) {
				size_t k = i * sx;
				for (size_t j=0; j<sx; j++) {
					d[k+j] = distance_lonlat(p[0][i], p[1][i], px[0][j], px[1][j], a, f);
				}
			}
		} else { // not reached
			for (size_t i=0; i<s; i++) {
				size_t k = i * sx;
				for (size_t j=0; j<sx; j++) {
					d[k+j] = distance_plane(p[0][i], p[1][i], px[0][j], px[1][j]) * m;
				}
			}
		} 
	}

	return d;
}




std::vector<double> broom_dist_planar(std::vector<double> &v, std::vector<double> &above, std::vector<double> res, std::vector<size_t> dim, double lindist) {

	double dx = res[0] * lindist;
	double dy = res[1] * lindist;
	double dxy = sqrt(dx * dx + dy *dy);


	size_t n = v.size();
	size_t nr = n / dim[0]; // must get entire rows
	size_t nc = dim[1];

	std::vector<double> dist(n, 0);

	//top to bottom
    //left to right

	if ( std::isnan(v[0]) ) { //first cell, no cell left of it
		dist[0] = above[0] + dy;
	}
	for (size_t i=1; i<nc; i++) { //first row, no row above it, use "above"
		if (std::isnan(v[i])) {
			dist[i] = std::min(std::min(above[i] + dy, above[i-1] + dxy), dist[i-1] + dx);
		}
	}
	for (size_t r=1; r<nr; r++) { //other rows
		size_t i=r*nc;
		if (std::isnan(v[i])) {
			dist[i] = dist[i-nc] + dy;
		}
		for (size_t i=r*nc+1; i<((r+1)*nc); i++) {
			if (std::isnan(v[i])) {
				dist[i] = std::min(std::min(dist[i-1] + dx, dist[i-nc] + dy), dist[i-nc-1] + dxy);
			}
		}
	}
		//right to left
	if ( std::isnan(v[nc-1])) { //first cell
		dist[nc-1] = std::min(dist[nc-1], above[nc-1] + dy);
	}
	for (size_t i=(nc-1); i > 0; i--) { // other cells on first row
		if (std::isnan(v[i-1])) {
			dist[i] = std::min(std::min(std::min(dist[i-1], above[i-1] + dy), above[i] + dxy), dist[i] + dx);
		}
	}
	for (size_t r=1; r<nr; r++) { // other rows
		size_t i=(r+1)*nc-1;
		if (std::isnan(v[i])) {
			dist[i] = std::min(dist[i], dist[i-nc] + dy);
		}
		for (size_t i=(r+1)*nc-2; i>(r*nc-1); i--) {
			if (std::isnan(v[i])) {
				dist[i] = std::min(std::min(std::min(dist[i], dist[i+1] + dx), dist[i-nc] + dy), dist[i-nc+1] + dxy);
			}
		}
	}
	return dist;
}

/*


//std::vector<double> broom_dist_geo(std::vector<double> &v, std::vector<double> &above, std::vector<double> res, std::vector<unsigned> dim, bool down) {
//
//}
*/


SpatRaster SpatRaster::gridDistance(SpatOptions &opt) {

	SpatRaster out = geometry();
	if (!hasValues()) {
		out.setError("cannot compute distance for a raster with no values");
		return out;
	}

	//bool isgeo = out.islonlat

	double m = source[0].srs.to_meter();
	m = std::isnan(m) ? 1 : m;

	std::vector<double> res = resolution();
	std::vector<size_t> dim = {nrow(), ncol()};

	SpatRaster first = out.geometry();

	std::string tempfile = "";
	std::vector<double> above(ncol(), std::numeric_limits<double>::infinity());
    std::vector<double> d, v, vv;

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}
	std::string filename = opt.get_filename();
	opt.set_filenames({""});
 	if (!first.writeStart(opt)) { return first; }

	for (size_t i = 0; i < first.bs.n; i++) {
        v = readBlock(first.bs, i);
        d = broom_dist_planar(v, above, res, dim, m);
		if (!first.writeValues(d, first.bs.row[i], first.bs.nrows[i], 0, ncol())) return first;
	}
	first.writeStop();

	if (!first.readStart()) {
		out.setError(first.getError());
		return(out);
	}

	opt.set_filenames({filename});

  	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	for (size_t i = out.bs.n; i>0; i--) {
        v = readBlock(out.bs, i-1);
		std::reverse(v.begin(), v.end());
        d = broom_dist_planar(v, above, res, dim, m);
		vv = first.readBlock(first.bs, i-1);
	    std::transform (d.rbegin(), d.rend(), vv.begin(), vv.begin(), [](double a, double b) {return std::min(a,b);});
		if (!out.writeValues(vv, out.bs.row[i-1], out.bs.nrows[i-1], 0, ncol())) return out;
	}
	out.writeStop();
	readStop();
	first.readStop();
	return(out);
}


/*
std::vector<double> do_edge(std::vector<double> &d, size_t nrow, size_t ncol, bool before, bool after, bool classes, bool inner, unsigned dirs) {

	bool falseval = 0;

	size_t n = nrow * ncol;
	std::vector<double> val(n, NAN);

	// main
	int r[8] = { -1,0,0,1 , -1,-1,1,1};
	int c[8] = { 0,-1,1,0 , -1,1,-1,1};
		// first col
	int fr[5] = {-1,0,1,-1,1};
	int fc[5] = { 0,1,0, 1,1};
		// last col
	int lr[5] = { -1, 0,1, -1, 1};
	int lc[5] = {  0,-1,0, -1,-1};


	// first row
	int br[5] = {  0,0,1 , 1,1};
	int bc[5] = { -1,1,0 ,-1,1};
		// first col
	int bfr[3] = { 0,1 ,1};
	int bfc[3] = { 1,0 ,1};
		// last col
	int blr[3] = {  0,1 , 1};
	int blc[3] = { -1,0 ,-1};


	// last row
	int ar[5] = { -1,0,0, -1,-1};
	int ac[5] = { 0,-1,1, -1, 1};
		// first col
	int afr[3] = { -1,0,-1};
	int afc[3] = { 0 ,1, 1};
		// last col
	int alr[3] = { -1,0, -1};
	int alc[3] = { 0,-1, -1};
	

	size_t rowoff = 0;
	size_t nrows = nrow;
	if (before) {
		rowoff = 1;
	}
	if (after) {
		nrows++;
	}
	size_t hrdirs = dirs == 4 ? 3 : 5;
	size_t hcdirs = dirs == 4 ? 2 : 3;

	if (classes) {  // by class

		for (size_t i = 1; i < (nrows); i++) {
			for (size_t j = 1; j < (ncol-1); j++) {
				size_t cell = i * ncol+j ;
				double test = d[cell + r[0] * ncol + c[0]];
				val[cell] = std::isnan(test) ? NAN : falseval;
				for (size_t k=1; k<dirs; k++) {
					double v = d[cell+r[k]*ncol +c[k]];
					if (std::isnan(test)) {
						if (!std::isnan(v)) {
							val[cell] = 1;
							break;
						}
					} else if (test != v) {
						val[cell] = 1;
						break;
					}
				}
			}
		}		
	} else { // not by class
		if (inner) {  ////// inner //// 	
			if (!before) { // no row above
				for (size_t j = 1; j < (ncol-1); j++) {
					// cell = j
					if (!std::isnan(d[j])) {
						val[j] = 0;
						for (size_t k=0; k < hrdirs; k++) {		
							if ( std::isnan(d[j + br[k] * ncol + bc[k] ])) {
								val[j] = 1;
								break;
							}
						}
					}
				} // first column of first row
				// cell = j = 0
				if (!std::isnan(d[0])) {
					val[0] = 0;
					for (size_t k=0; k < hcdirs; k++) {		
						if ( std::isnan(d[bfr[k] * ncol + bfc[k] ])) {
							val[0] = 1;
							break;
						}
					}
				} // last column of first row
				size_t cell = ncol-1;
				if (!std::isnan(d[cell])) {
					val[cell] = 0;
					for (size_t k=0; k < hcdirs; k++) {		
						if ( std::isnan(d[cell + blr[k] * ncol + blc[k] ])) {
							val[cell] = 1;
							break;
						}
					}
				}
			}
			if (!after) { // no row below
				size_t i = nrows-1;
				for (size_t j = 1; j < (ncol-1); j++) {
					size_t cell = i * ncol + j;
					size_t outcell = (i-rowoff) * ncol + j;
					if (!std::isnan(d[cell])) {
						val[outcell] = 0;
						for (size_t k=0; k < hrdirs; k++) {		
							if ( std::isnan(d[cell+ ar[k] * ncol + ac[k] ])) {
								val[outcell] = 1;
								break;
							}
						}
					}
				} // first cell for last row
				size_t cell = (nrows-1) * ncol;
				size_t outcell = (nrows-1-rowoff) * ncol;
				if (!std::isnan(d[cell])) {
					val[outcell] = 0;
					for (size_t k=0; k < hcdirs; k++) {		
						if ( std::isnan(d[cell + afr[k] * ncol + afc[k] ])) {
							val[outcell] = 1;
							break;
						}
					}
				} // last cell for last row
				cell += ncol-1;
				outcell += ncol-1;
				if (!std::isnan(d[cell])) {
					val[outcell] = 0;
					for (size_t k=0; k < hcdirs; k++) {		
						if ( std::isnan(d[cell+ alr[k] * ncol + alc[k] ])) {
							val[outcell] = 1;
							break;
						}
					}
				}	
			} // other rows 

			
			for (size_t i = 1; i < nrows; i++) {
				for (size_t j = 1; j < (ncol-1); j++) {
					size_t cell = i * ncol + j;
					if (!std::isnan(d[cell])) {
						size_t outcell = (i-rowoff) * ncol + j;
						val[outcell] = 0;
						for (size_t k=0; k < dirs; k++) {		
							if ( std::isnan(d[cell+ r[k] * ncol + c[k] ])) {
								val[outcell] = 1;
								break;
							}
						}
					}
				}

				// first column
				size_t cell = i * ncol;
				size_t outcell = (i-rowoff) * ncol;
				if (!std::isnan(d[cell])) {
					val[outcell] = 0;
					for (size_t k=0; k < hrdirs; k++) {		
						if ( std::isnan(d[cell + fr[k] * ncol + fc[k] ])) {
							val[outcell] = 1;
							break;
						}
					}
				}
				// last column
				cell += ncol - 1;
				outcell += ncol - 1;
				if (!std::isnan(d[cell])) {
					val[outcell] = 0;
					for (size_t k=0; k < hrdirs; k++) {		
						if ( std::isnan( d[cell + lr[k] * ncol + lc[k] ])) {
							val[outcell] = 1;
							break;
						}
					}
				}
			}

		} else { ////// outer //// 	

	
			if (!before) {
				for (size_t j = 1; j < (ncol-1); j++) {
					size_t cell = j;
					if (std::isnan(d[cell])) {
						val[cell] = NAN;
						for (size_t k=0; k < hcdirs; k++) {		
							if ( !std::isnan(d[j + br[k] * ncol + bc[k] ])) {
								val[cell] = 1;
								break;
							}
						}
					} else {
						val[cell] = 0;
					}
				}
			}
			if (!after) {
				size_t i = (nrow - 1) * ncol;
				for (size_t j = 1; j < (ncol-1); j++) {
					size_t cell = (i-rowoff) * ncol + j;
					if (std::isnan(d[cell])) {
						val[cell] = NAN;
						for (size_t k=0; k < hcdirs; k++) {		
							if (!std::isnan(d[cell+ ar[k] * ncol + ac[k] ])) {
								val[cell] = 1;
								break;
							}
						}
					} else {
						val[cell] = 0;
					}
				}
			}

			for (size_t i = 1; i < nrows; i++) {
				for (size_t j = 1; j < (ncol-1); j++) {
					size_t cell = (i-rowoff) * ncol + j;
					if (std::isnan(d[cell])) {
						val[cell] = NAN;
						for (size_t k=0; k<dirs; k++) {
							if (!std::isnan(d[cell + r[k] * ncol + c[k]])) {
								val[cell] = 1;
								break;
							}
						}
					} else {
						val[cell] = 0;
					}
				}
			}
		}
	
	}
//	val.erase(val.begin(), val.begin()+ncol);
//	val.erase(val.end()-ncol, val.end());	
	return(val);
}

*/

/*
std::vector<double> get_border(std::vector<double> xd, size_t nrows, size_t ncols, bool classes, std::string edgetype, unsigned dirs) {

	size_t n = nrows * ncols;

	std::vector<double> xval(n, 0);
	Rcpp::Rcout << "hello" << std::endl;

	int r[8] = {-1,0,0,1, -1,-1,1,1};
	int c[8] = {0,-1,1,0, -1,1,-1,1};
	int falseval = 0;

	if (!classes) {
		if (edgetype == "inner") { 
			for (size_t i = 1; i < (nrows-1); i++) {
				for (size_t j = 1; j < (ncols-1); j++) {
					size_t cell = i*ncols+j;
					if (std::isnan(xd[cell])) {
						xval[cell] = NAN;
					} else {
						xval[cell] = falseval;
						for (size_t k=0; k< dirs; k++) {
							if (std::isnan (xd[cell + r[k] * ncols + c[k]])) {
								xval[cell] = 1;
								break;
							}
						}
					}
				}
			}
	
		} else { // if (edgetype == "outer"
			for (size_t i = 1; i < (nrows-1); i++) {
				for (size_t j = 1; j < (ncols-1); j++) {
					size_t cell = i*ncols+j;
					xval[cell] = falseval;
					if (std::isnan(xd[cell])) {
						xval[cell] = NAN;
					} else {
						for (size_t k=0; k < dirs; k++) {		
							if (std::isnan(xd[cell+ r[k] * ncols + c[k] ])) {
								xval[cell] = 1;
								break;
							}
						}
					}
				}
			}
		} 
	} else { // by class
		for (size_t i = 1; i < (nrows-1); i++) {
			for (size_t j = 1; j < (ncols-1); j++) {
				size_t cell = i*ncols+j;
				double test = xd[ cell + r[0]*ncols + c[0] ];
				if (std::isnan(test)) {
					xval[cell] = NAN;
				} else {
					xval[cell] = falseval;
					for (size_t k=1; k < dirs; k++) {
						if (test != xd[ cell+r[k]*ncols +c[k] ]) {
							xval[cell] = 1;
							break;
						}
					}
				}
			}
		}

	}
	return(xval);
}
*/


/*
SpatRaster SpatRaster::edges(bool classes, std::string type, unsigned directions, SpatOptions &opt) {

	SpatRaster out = geometry();
	if (nlyr() > 1) {
		out.setError("boundary detection can only be done for one layer at a time --- to be improved");
		return(out);
	}
	if (!hasValues()) {
		out.setError("SpatRaster has no values");
		return out;
	}

	
	if ((directions != 4) && (directions != 8)) {
		out.setError("directions should be 4 or 8");
		return(out);	
	}
	if ((type != "inner") && (type != "outer")) {
		out.setError("directions should be 'inner' or 'outer'");
		return(out);	
	}
	bool inner = type == "inner";

	size_t nc = ncol();

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}

 	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	
	for (size_t i = 0; i < out.bs.n; i++) {
		std::vector<double> v;
		bool before = false;
		bool after = false;
		if (i == 0) {
			if (out.bs.n == 1) {
				v = readValues(out.bs.row[i], out.bs.nrows[i], 0, nc);
			} else {
				v = readValues(out.bs.row[i], out.bs.nrows[i]+1, 0, nc);
				after = true;
			}	
		} else {
			before = true;
			if (i == out.bs.n) {
				v = readValues(out.bs.row[i]-1, out.bs.nrows[i]+1, 0, nc);
			} else {
				v = readValues(out.bs.row[i]-1, out.bs.nrows[i]+2, 0, nc);
				after = true;
			}
		}
		std::vector<double> vv = do_edge(v, out.bs.nrows[i], nc, before, after, classes, inner, directions);
		if (!out.writeValues(vv, out.bs.row[i], out.bs.nrows[i], 0, nc)) return out;
	}
	out.writeStop();
	readStop();

	return(out);
}
*/

std::vector<double> do_edge(const std::vector<double> &d, const size_t nrow, const size_t ncol, const bool classes, const bool inner, const unsigned dirs) {

	bool falseval = 0;
	
	
	size_t n = nrow * ncol;
	std::vector<double> val(n);
	
	int r[8] = { -1,0,0,1 , -1,-1,1,1};
	int c[8] = { 0,-1,1,0 , -1,1,-1,1};	
	
	if (!classes) {
		if (inner) { // inner
			for (size_t i = 1; i < (nrow-1); i++) {
				for (size_t j = 1; j < (ncol-1); j++) {
					size_t cell = i*ncol+j;
					val[cell] = NAN;
					if ( !std::isnan(d[cell])) {
						val[cell] = falseval;
						for (size_t k=0; k< dirs; k++) {
							if ( std::isnan(d[cell + r[k] * ncol + c[k]])) {
								val[cell] = 1;
								break;
							}
						}
					}
				}
			}
		
		} else { //outer
			for (size_t i = 1; i < (nrow-1); i++) {
				for (size_t j = 1; j < (ncol-1); j++) {
					size_t cell = i*ncol+j;
					val[cell] = falseval;
					if (std::isnan(d[cell])) {
						val[cell] = NAN;
						for (size_t k=0; k < dirs; k++) {			
							if ( !std::isnan(d[cell+ r[k] * ncol + c[k] ])) {
								val[cell] = 1;
								break;
							}
						}
					}
				}
			}
		} 
	} else { // by class
		for (size_t i = 1; i < (nrow-1); i++) {
			for (size_t j = 1; j < (ncol-1); j++) {
				size_t cell = i*ncol+j;
				double test = d[cell+r[0]*ncol+c[0]];
				val[cell] = std::isnan(test) ? NAN : falseval;
				for (size_t k=1; k<dirs; k++) {
					double v = d[cell+r[k]*ncol +c[k]];
					if (std::isnan(test)) {
						if (!std::isnan(v)) {
							val[cell] = 1;
							break;
						}
					} else if (test != v) {
						val[cell] = 1;
						break;
					}
				}
			}
		}

	}
	return(val);
}



void addrowcol(std::vector<double> &v, size_t nr, size_t nc, bool rowbefore, bool rowafter, bool cols) {
	
	if (rowbefore) {
		v.insert(v.begin(), v.begin(), v.begin()+nc);
		nr++;
	}
	if (rowafter) {
		v.insert(v.end(), v.end()-nc, v.end());
		nr++;
	}
	if (cols) {
		for (size_t i=0; i<nr; i++) {
			size_t j = i*(nc+2);
			v.insert(v.begin()+j+nc, v[j+nc-1]);
			v.insert(v.begin()+j, v[j]);
		}
	}
}


void striprowcol(std::vector<double> &v, size_t nr, size_t nc, bool rows, bool cols) {
	if (rows) {
		v.erase(v.begin(), v.begin()+nc);
		v.erase(v.end()-nc, v.end());
		nr -= 2;
	}
	if (cols) {
		nc -= 2;
		for (size_t i=0; i<nr; i++) {
			size_t j = i*nc;
			v.erase(v.begin()+j);
			v.erase(v.begin()+j+nc);
		}
	}
}


SpatRaster SpatRaster::edges(bool classes, std::string type, unsigned directions, SpatOptions &opt) {

	SpatRaster out = geometry();
	if (nlyr() > 1) {
		out.setError("boundary detection can only be done for one layer at a time --- to be improved");
		return(out);
	}
	if (!hasValues()) {
		out.setError("SpatRaster has no values");
		return out;
	}

	
	if ((directions != 4) && (directions != 8)) {
		out.setError("directions should be 4 or 8");
		return(out);	
	}
	if ((type != "inner") && (type != "outer")) {
		out.setError("directions should be 'inner' or 'outer'");
		return(out);	
	}
	bool inner = type == "inner";

	size_t nc = ncol();
	size_t nr = nrow();
	

	if (!readStart()) {
		out.setError(getError());
		return(out);
	}

 	if (!out.writeStart(opt)) {
		readStop();
		return out;
	}
	
	for (size_t i = 0; i < out.bs.n; i++) {
		std::vector<double> v;
		//bool before = false;
		//bool after = false;
		if (i == 0) {
			if (out.bs.n == 1) {
				v = readValues(out.bs.row[i], out.bs.nrows[i], 0, nc);
				addrowcol(v, nr, nc, true, true, true);			
			} else {
				v = readValues(out.bs.row[i], out.bs.nrows[i]+1, 0, nc);
				addrowcol(v, nr, nc, true, false, true);			
				//after = true;
			}	
		} else {
			//before = true;
			if (i == out.bs.n) {
				v = readValues(out.bs.row[i]-1, out.bs.nrows[i]+1, 0, nc);
				addrowcol(v, nr, nc, false, true, true);			
			} else {
				v = readValues(out.bs.row[i]-1, out.bs.nrows[i]+2, 0, nc);
				addrowcol(v, nr, nc, false, false, true);			
				//after = true;
			}
		}
		//before, after, 
		std::vector<double> vv = do_edge(v, out.bs.nrows[i]+2, nc+2, classes, inner, directions);
		striprowcol(vv, out.bs.nrows[i]+2, nc+2, true, true);
		if (!out.writeValues(vv, out.bs.row[i], out.bs.nrows[i], 0, nc)) return out;
	}
	out.writeStop();
	readStop();

	return(out);
}



SpatRaster SpatRaster::buffer(double d, SpatOptions &opt) {
	SpatRaster out = geometry();
	if (d <= 0) {
		out.setError("buffer size <= 0; nothing to compute");
		return out;
	}
	SpatOptions ops;
	if (nlyr() > 1) {
		out.addWarning("buffer computations can only be done for one layer at a time --- to be improved");
		std::vector<unsigned> lyr = {0};
		subset(lyr, ops);
	}
	std::string etype = "inner";
	SpatRaster e = edges(false, etype, 8, ops);
	SpatVector p = e.as_points(false, true, opt);
	out = out.distance(p, ops);
	out = out.arith(d, "<=", false, opt);
	return out;
}




SpatVector SpatVector::point_buffer(double d, unsigned quadsegs) { 

	std::vector<std::vector<double>> xy = coordinates();
	SpatVector out;
	out.srs = srs;
	size_t n = quadsegs * 4;
	std::vector<double> px(n);
	std::vector<double> py(n);
	double step = 360.0 / n;
	SpatGeom g(polygons);
	g.addPart(SpatPart(0, 0));
	size_t npts = size();

	if (is_geographic()) {
		double a=6378137;
		double f=1/298.257223563;
		std::vector<double> brng(n);
		for (size_t i=0; i<n; i++) {
			brng[i] = i * step;
		}
		for (size_t i=0; i<npts; i++) {
			if (std::isnan(xy[0][i]) || std::isnan(xy[1][i])) {
				out.addGeom(SpatGeom(polygons));
			} else {
				std::vector<std::vector<double>> dp = destpoint_lonlat(xy[0][i], xy[1][i], brng, d, a, f);
				//close polygons
				dp[0].push_back(dp[0][0]);
				dp[1].push_back(dp[1][0]);
				g.setPart(SpatPart(dp[0], dp[1]), 0);
				out.addGeom(g);
			}
		}

	} else {
		std::vector<double> cosb(n);
		std::vector<double> sinb(n);
		for (size_t i=0; i<n; i++) {
			double brng = i * step;
			brng = toRad(brng);
			cosb[i] = d * cos(brng);
			sinb[i] = d * sin(brng);
		}
		for (size_t i=0; i<npts; i++) {
			if (std::isnan(xy[0][i]) || std::isnan(xy[1][i])) {
				out.addGeom(SpatGeom(polygons));
			} else {
				for (size_t j=0; j<n; j++) {
					px[j] = xy[0][i] + cosb[j];
					py[j] = xy[1][i] + sinb[j];
				}
				//close polygons
				px.push_back(px[0]);
				py.push_back(py[0]);
				g.setPart(SpatPart(px, py), 0);
				out.addGeom(g);
			}
		}
	}
	return(out);
}




SpatVector SpatVector::buffer(double d, unsigned segments, unsigned capstyle){
	SpatVector out;
	std::string vt = type();
	if (vt != "points") {
		out.setError("must be points");
		return out;
	}
	if ((vt == "points") && (d <= 0)) {
		out.setError("buffer size must be >= 0 with points");
		return out;
	}
	out = point_buffer(d, segments); 
	return out;
}

