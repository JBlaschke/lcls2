//-----------------------------

#include "../include/PeakFinderAlgos.h"
#include <sstream>   // for stringstream
#include <cmath>     // floor, ceil
#include <iomanip>   // for std::typedef

//-----------------------------

//setw psalgos::types::TwoIndexes TwoIndexes;
//typedef psalgos::localextrema::TwoIndexes TwoIndexes;

using namespace std;

//-----------------------------

namespace psalgos {

//-----------------------------

PeakFinderAlgos::PeakFinderAlgos(const size_t& seg, const unsigned& pbits)
  : m_seg(seg)
  , m_pbits(pbits)
  , m_local_maxima(0)
  , m_local_minima(0)
  , m_conmap(0)
  , m_peak_npix_min(0)
  , m_peak_npix_max(1e6)
  , m_peak_amax_thr(0)
  , m_peak_atot_thr(0)
  , m_peak_son_min(0)
{
  if(m_pbits & LOG::DEBUG) std::cout << "in c-tor PeakFinderAlgos\n";
}

//-----------------------------

PeakFinderAlgos::~PeakFinderAlgos() 
{
  std::cout << "in d-tor ~PeakFinderAlgos\n";
  if(m_pbits & LOG::DEBUG) std::cout << "in d-tor ~PeakFinderAlgos\n";
  if (m_local_maxima) delete[] m_local_maxima;
  if (m_local_minima) delete[] m_local_minima;
  if (m_conmap)       delete[] m_conmap;
}

void
PeakFinderAlgos::setHeap(Heap& heap) { m_heap = heap; }

//-----------------------------

void
PeakFinderAlgos::printParameters()
{
  std::stringstream ss; 
  ss << "PeakFinderAlgos::printParameters\n";
  ss << "seg   " << m_seg << '\n';
  ss << "pbits " << m_pbits << '\n';
  ss << "rows  " << m_rows << '\n';
  ss << "cols  " << m_cols << '\n';
  ss << "rank  " << m_rank << '\n';
  ss << "r0    " << m_r0 << '\n';
  ss << "dr    " << m_dr << '\n';
  ss << "nsigm " << m_nsigm << '\n';
  ss << "img_size " << m_img_size << '\n';
  ss << "sizeof(extrim_t) " << sizeof(extrim_t) << '\n';
  cout << ss.str();
}

//-----------------------------

void PeakFinderAlgos::_convPeaksSelected(){
    numPeaksSelected = arr_peaks_sel_drp.num_elem(); //v_peaks_sel_drp.len;
    rows = Array<float>(m_heap, numPeaksSelected*sizeof(float), 1);
    rows.shape(numPeaksSelected);
    cols = Array<float>(m_heap, numPeaksSelected*sizeof(float), 1);
    cols.shape(numPeaksSelected);
    intens = Array<float>(m_heap, numPeaksSelected*sizeof(float), 1);
    intens.shape(numPeaksSelected);
    for(unsigned i = 0; i< numPeaksSelected; i++){
        //const Peak *p = v_peaks_sel_drp.data[i];
        //rows(i) = p->row_cgrav;
        //cols(i) = p->col_cgrav;
        //intens(i) = p->amp_tot;
        const Peak p = arr_peaks_sel_drp(i);
        rows(i) = p.row_cgrav;
        cols(i) = p.col_cgrav;
        intens(i) = p.amp_tot;
    }
}

/*
void 
PeakFinderAlgos::_initMapsAndVectors()
{
  if(m_pbits & LOG::DEBUG) std::cout << "in _initMapsAndVectors\n";

  if (m_conmap==0) m_conmap = new conmap_t[m_img_size];

  std::fill_n(m_conmap, int(m_img_size), conmap_t(0));

  if(v_ind_pixgrp.capacity() != m_pixgrp_max_size) {
      v_ind_pixgrp.reserve(m_pixgrp_max_size);
  }

  if(vv_peak_pixinds.capacity() < m_npksmax) vv_peak_pixinds.reserve(m_npksmax);
  vv_peak_pixinds.clear();

  if(v_peaks.capacity() < m_npksmax) v_peaks.reserve(m_npksmax);
  v_peaks.clear();

  _evaluateRingIndexes();

}*/

void 
PeakFinderAlgos::_initMapsAndVectors_drp()
{

  if(m_pbits & LOG::DEBUG) std::cout << "in _initMapsAndVectors_drp\n";

  if (m_conmap==0) m_conmap = new conmap_t[m_img_size];

  std::fill_n(m_conmap, int(m_img_size), conmap_t(0));

  //if(v_ind_pixgrp_drp.len != m_pixgrp_max_size) {
  //    v_ind_pixgrp_drp.capacity = m_pixgrp_max_size;
  //}

  if (arr_ind_pixgrp_drp.rank() == 0) {
    arr_ind_pixgrp_drp = Array<TwoIndexes>(m_heap, m_pixgrp_max_size*sizeof(TwoIndexes), 1);
  } else {
    arr_ind_pixgrp_drp.shape(0);
  }

  //if(vv_peak_pixinds_drp.len < m_npksmax) vv_peak_pixinds_drp.capacity = m_npksmax;
  //vv_peak_pixinds_drp.len = 0;
  if (aa_peak_pixinds_drp.rank() == 0) {
    aa_peak_pixinds_drp = Array<Array<TwoIndexes> >(m_heap, m_npksmax*sizeof(Array<TwoIndexes>), 1);
  } else {
    aa_peak_pixinds_drp.shape(0);
  }

  //if(v_peaks_drp.len < m_npksmax) v_peaks_drp.capacity = m_npksmax;
  //v_peaks_drp.len = 0;
  if (arr_peaks_drp.rank() == 0) {
    arr_peaks_drp = Array<Peak>(m_heap, m_npksmax*sizeof(Peak), 1);
  } else {
    arr_peaks_drp.shape(0);
  }

  _evaluateRingIndexes_drp();

  //if (firstInit) firstInit = false;

}

//-----------------------------
/*
void 
PeakFinderAlgos::_evaluateRingIndexes()
{
  if(m_pbits & LOG::DEBUG) std::cout << "in _evaluateRingIndexes, r0=" << m_r0 << " dr=" << m_dr << '\n';

  int indmax = (int)std::ceil(m_r0 + m_dr);
  int indmin = -indmax;
  unsigned npixmax = (2*indmax+1)*(2*indmax+1);
  if(v_indexes.capacity() < npixmax) v_indexes.reserve(npixmax);
  v_indexes.clear();

  for (int i = indmin; i <= indmax; ++ i) {
    for (int j = indmin; j <= indmax; ++ j) {
      double r = std::sqrt(double(i*i + j*j));
      if (r < m_r0 || r > m_r0 + m_dr) continue;
      v_indexes.push_back(TwoIndexes(i,j));
    }
  }

  if(m_pbits) {//m_pbits & LOG::INFO) { // I like these debug messages
    printMatrixOfRingIndexes();
    printVectorOfRingIndexes();
  }
}
*/
void 
PeakFinderAlgos::_evaluateRingIndexes_drp()
{
  if(m_pbits & LOG::DEBUG) std::cout << "in _evaluateRingIndexes_drp, r0=" << m_r0 << " dr=" << m_dr << '\n';

  int indmax = (int)std::ceil(m_r0 + m_dr);
  int indmin = -indmax;

  //if(v_indexes_drp.capacity < npixmax) v_indexes_drp.capacity = npixmax;
  //v_indexes_drp.len = 0;
  if (arr_indexes_drp.rank()==0) {
    arr_indexes_drp = Array<TwoIndexes>(m_heap, (indmax-indmin+1)*(indmax-indmin+1)*sizeof(TwoIndexes), 1);
  } else {
    arr_indexes_drp.shape(0);
  }

  for (int i = indmin; i <= indmax; ++ i) {
    for (int j = indmin; j <= indmax; ++ j) {
      double r = std::sqrt(double(i*i + j*j));
      if (r < m_r0 || r > m_r0 + m_dr) continue;
      arr_indexes_drp.push_back(TwoIndexes(i,j));
    }
  }

  if(m_pbits) {
    printMatrixOfRingIndexes();
    printVectorOfRingIndexes_drp();
  }
}

//-----------------------------

void 
PeakFinderAlgos::printMatrixOfRingIndexes()
{
  int indmax = (int)std::ceil(m_r0 + m_dr);
  int indmin = -indmax;
  unsigned counter = 0;
  std::stringstream ss; 
  
  for (int i = indmin; i <= indmax; ++ i) {
    for (int j = indmin; j <= indmax; ++ j) {
      double r = std::sqrt(double(i*i + j*j));
      int status = (r < m_r0 || r > m_r0 + m_dr) ? 0 : 1;
      if (status) counter++;
      if (i==0 && j==0) ss << " +";
      else              ss << " " << status;
    }
    ss << '\n';
  }
  ss << "Number of pixels to estimate background = " << counter << '\n';
  cout << ss.str();
}

//-----------------------------

/*
void 
PeakFinderAlgos::printVectorOfRingIndexes()
{
  if(v_indexes.empty()) {
    _evaluateRingIndexes();
  }

  std::stringstream ss; 
  ss << "In printVectorOfRingIndexes:\n Vector size: " << v_indexes.size() << '\n';
  int counter_in_line=0;
  for(vector<TwoIndexes>::const_iterator ij  = v_indexes.begin();
                                          ij != v_indexes.end(); ij++) {
    ss << " (" << ij->i << "," << ij->j << ')';
    if (++counter_in_line > 9) {ss << '\n'; counter_in_line=0;}
  }   
  cout << ss.str() << '\n';
  //MsgLog(_name(), info, ss.str());
}*/

void 
PeakFinderAlgos::printVectorOfRingIndexes_drp()
{
  if(arr_indexes_drp.num_elem() == 0) {
    _evaluateRingIndexes_drp();
  }

  std::stringstream ss; 
  ss << "In printVectorOfRingIndexes:\n Vector size: " << arr_indexes_drp.num_elem() << '\n';
  int counter_in_line=0;
  for (unsigned int ii = 0; ii < arr_indexes_drp.num_elem(); ii++) {
    ss << " (" << arr_indexes_drp(ii).i << "," << arr_indexes_drp(ii).j << ')';
    if (++counter_in_line > 9) {ss << '\n'; counter_in_line=0;}
  }   
  cout << ss.str() << '\n';
}
//-----------------------------

void
PeakFinderAlgos::printSelectionPars()
{
  std::stringstream ss; 
  ss << "PeakFinderAlgos::printSelectionPars(), seg=" << m_seg << '\n';
  ss << "  npix_min" << m_peak_npix_min << '\n';
  ss << "  npix_max" << m_peak_npix_max << '\n';
  ss << "  amax_thr" << m_peak_amax_thr << '\n';
  ss << "  atot_thr" << m_peak_atot_thr << '\n';
  ss << "  son_min " << m_peak_son_min  << '\n';
  cout << ss.str();
}

//-----------------------------

void
PeakFinderAlgos::setPeakSelectionPars(const float& npix_min, const float& npix_max,
                                      const float& amax_thr, const float& atot_thr, const float& son_min)
{
  if(m_pbits & LOG::DEBUG) cout << "in setPeakSelectionPars, seg=" << m_seg << '\n';
  m_peak_npix_min = npix_min;
  m_peak_npix_max = npix_max;
  m_peak_amax_thr = amax_thr;
  m_peak_atot_thr = atot_thr;
  m_peak_son_min  = son_min;
}

//-----------------------------

bool
PeakFinderAlgos::_peakIsSelected(const Peak& peak)
{
  if (peak.son     < m_peak_son_min)  return false;
  if (peak.npix    < m_peak_npix_min) return false;
  if (peak.npix    > m_peak_npix_max) return false;
  if (peak.amp_max < m_peak_amax_thr) return false;
  if (peak.amp_tot < m_peak_atot_thr) return false;
  return true;
}

//-----------------------------

/*
void
PeakFinderAlgos::_makeVectorOfSelectedPeaks()
{
  if(v_peaks_sel.capacity() < m_npksmax) v_peaks_sel.reserve(m_npksmax);
     v_peaks_sel.clear();

  for(std::vector<Peak>::iterator it=v_peaks.begin(); it!=v_peaks.end(); ++it) { 
    Peak& peak = (*it);
    if(_peakIsSelected(peak)) {
        v_peaks_sel.push_back(peak);
    }
  }
  if(m_pbits) {
      std::cout << "_makeVectorOfSelectedPeaks, seg=" << m_seg 
                                    << "  #peaks raw=" << v_peaks.size() 
				    << "  sel=" << v_peaks_sel.size() << '\n';
  }
}
*/

void
PeakFinderAlgos::_makeVectorOfSelectedPeaks_drp()
{
  //if(v_peaks_sel_drp.capacity < m_npksmax) v_peaks_sel_drp.capacity = m_npksmax;
  //v_peaks_sel_drp.len = 0;
  if (arr_peaks_sel_drp.rank() == 0) {
    arr_peaks_sel_drp = Array<Peak>(m_heap, arr_peaks_drp.num_elem()*sizeof(Peak), 1);
  } else {
    arr_peaks_sel_drp.shape(0);
  }

  for(unsigned int ii = 0; ii < arr_peaks_drp.num_elem(); ii++) {
    Peak peak = arr_peaks_drp(ii);
    if(_peakIsSelected(peak)) {
        //v_peaks_sel_drp.data[v_peaks_sel_drp.len++] = peak;
        arr_peaks_sel_drp.push_back(peak);
    }
  }
  if(m_pbits) std::cout << "_makeVectorOfSelectedPeaks, seg=" << m_seg 
                        << "  #peaks raw=" << arr_peaks_drp.num_elem()
				        << "  sel=" << arr_peaks_sel_drp.num_elem() << '\n';
}


//-----------------------------

void
PeakFinderAlgos::_printVectorOfPeaks_drp(Array<Peak> v) {
  for(unsigned int ii = 0; ii < v.num_elem(); ii++) {
    const Peak p = v(ii);
    std::cout << fixed
       << "Seg:"      << std::setw(3) << std::setprecision(0) << p.seg
       << " Row:"     << std::setw(4) << std::setprecision(0) << p.row
       << " Col:"     << std::setw(4) << std::setprecision(0) << p.col
       << " Npix:"    << std::setw(3) << std::setprecision(0) << p.npix
       << " Imax:"    << std::setw(7) << std::setprecision(1) << p.amp_max
       << " Itot:"    << std::setw(7) << std::setprecision(1) << p.amp_tot
       << " CGrav r:" << std::setw(6) << std::setprecision(1) << p.row_cgrav
       << " c:"       << std::setw(6) << std::setprecision(1) << p.col_cgrav
       << " Sigma r:" << std::setw(5) << std::setprecision(2) << p.row_sigma
       << " c:"       << std::setw(5) << std::setprecision(2) << p.col_sigma
       << " Rows["    << std::setw(4) << std::setprecision(0) << p.row_min
       << ":"         << std::setw(4) << std::setprecision(0) << p.row_max
       << "] Cols["   << std::setw(4) << std::setprecision(0) << p.col_min
       << ":"         << std::setw(4) << std::setprecision(0) << p.col_max
       << "] B:"      << std::setw(5) << std::setprecision(1) << p.bkgd
       << " N:"       << std::setw(5) << std::setprecision(1) << p.noise
       << " S/N:"     << std::setw(5) << std::setprecision(1) << p.son
       << std::endl;
  }
}

//-----------------------------
//-- NON-CLASS METHODS
//-----------------------------

  std::ostream& 
  operator<<(std::ostream& os, const Peak& p) 
  {
    os << fixed
       << "Seg:"      << std::setw(3) << std::setprecision(0) << p.seg
       << " Row:"     << std::setw(4) << std::setprecision(0) << p.row 	     
       << " Col:"     << std::setw(4) << std::setprecision(0) << p.col 	      
       << " Npix:"    << std::setw(3) << std::setprecision(0) << p.npix    
       << " Imax:"    << std::setw(7) << std::setprecision(1) << p.amp_max     	      
       << " Itot:"    << std::setw(7) << std::setprecision(1) << p.amp_tot    	      
       << " CGrav r:" << std::setw(6) << std::setprecision(1) << p.row_cgrav 	      
       << " c:"       << std::setw(6) << std::setprecision(1) << p.col_cgrav   	      
       << " Sigma r:" << std::setw(5) << std::setprecision(2) << p.row_sigma  	      
       << " c:"       << std::setw(5) << std::setprecision(2) << p.col_sigma  	      
       << " Rows["    << std::setw(4) << std::setprecision(0) << p.row_min    	      
       << ":"         << std::setw(4) << std::setprecision(0) << p.row_max    	      
       << "] Cols["   << std::setw(4) << std::setprecision(0) << p.col_min    	      
       << ":"         << std::setw(4) << std::setprecision(0) << p.col_max    	     
       << "] B:"      << std::setw(5) << std::setprecision(1) << p.bkgd       	      
       << " N:"       << std::setw(5) << std::setprecision(1) << p.noise      	     
       << " S/N:"     << std::setw(5) << std::setprecision(1) << p.son;
    return os;
  }

//-----------------------------

  std::ostream& 
  operator<<(std::ostream& os, const RingAvgRms& o) 
  {
    os << fixed
       << " Bkgd avg:" << std::setw(7) << std::setprecision(1) << o.avg
       << " RMS:"      << std::setw(7) << std::setprecision(1) << o.rms
       << " Npix:"     << std::setw(4) << std::setprecision(0) << o.npx;
    return os;
  }

//-----------------------------

//template void PeakFinderAlgos::_findConnectedPixelsInRegionVX<float>(const float*, const int&, const int&);
//template void PeakFinderAlgos::_findConnectedPixelsInRegionVX<double>(const double*, const int&, const int&);
//template void PeakFinderAlgos::_findConnectedPixelsInRegionVX<int>(const int*, const int&, const int&);
//template void PeakFinderAlgos::_findConnectedPixelsInRegionVX<int16_t>(const int16_t*, const int&, const int&);
//template void PeakFinderAlgos::_findConnectedPixelsInRegionVX<uint16_t>(const uint16_t*, const int&, const int&);

//-----------------------------
} // namespace psalgos
//-----------------------------
