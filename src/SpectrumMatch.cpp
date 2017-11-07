#include <algorithm>
#include <math.h>
#include <tuple>

#include "SpectrumMatch.h"
using namespace ann_solo;

SpectrumSpectrumMatch* SpectrumMatcher::dot(
    Spectrum *query, std::vector<Spectrum*> candidates, double fragment_mz_tolerance, bool allow_shift)
{
    // compute a dot product score between the query spectrum and each candidate spectrum
    SpectrumSpectrumMatch *best_match = NULL;
    for(unsigned int candidate_index = 0; candidate_index < candidates.size(); candidate_index++)
    {
        Spectrum *candidate = candidates[candidate_index];

        // compile a list of (potentially shifted) peaks for the candidate spectrum
        std::vector<Peak> candidate_peaks;
        for(unsigned int peak_index = 0; peak_index < candidate->getNumPeaks(); peak_index++)
        {
            // unshifted peaks are always allowed to match irrespective of their charge: charge 0
            candidate_peaks.push_back(Peak(candidate->getPeakMass(peak_index),
                                           candidate->getPeakIntensity(peak_index), 0, peak_index));
        }
        // add shifted peaks if necessary
        double mass_dif = (query->getPrecursorMz() - candidate->getPrecursorMz()) * candidate->getPrecursorCharge();
        if(allow_shift && mass_dif > fragment_mz_tolerance)
        {
            for(unsigned int peak_index = 0; peak_index < candidate->getNumPeaks(); peak_index++)
            {
                // peaks with a known charge are shifted with a mass difference corresponding to this charge
                unsigned int min_charge = 0;
                unsigned int max_charge = 0;
                if(candidate->getPeakCharge(peak_index) > 0)
                {
                    min_charge = candidate->getPeakCharge(peak_index);
                    max_charge = min_charge + 1;
                }
                // peaks without a known charge are shifted with a mass difference corresponding to all charges up to the precursor charge
                else
                {
                    min_charge = 1;
                    max_charge = candidate->getPrecursorCharge();
                }
                // create shifted peaks
                for(unsigned int charge = min_charge; charge < max_charge; charge++)
                {
                    double mass = candidate->getPeakMass(peak_index) - mass_dif / charge;
                    if(mass > 0)
                    {
                        candidate_peaks.push_back(Peak(mass, candidate->getPeakIntensity(peak_index), charge, peak_index));
                    }
                }
            }
            std::sort(candidate_peaks.begin(), candidate_peaks.end(), [](auto &peak1, auto &peak2) {
                      return peak1.getMass() < peak2.getMass(); });
        }

        // find the matching peaks between the query spectrum and the candidate spectrum
        std::vector<std::tuple<float, unsigned int, unsigned int>> peak_matches;
        unsigned int candidate_peak_index = 0;
        for(unsigned int query_peak_index = 0; query_peak_index < query->getNumPeaks(); query_peak_index++)
        {
            float query_peak_mass = query->getPeakMass(query_peak_index);
            float query_peak_intensity = query->getPeakIntensity(query_peak_index);
            unsigned int query_peak_charge = query->getPeakCharge(query_peak_index);
            // advance while there is an excessive mass difference
            while(candidate_peak_index < candidate_peaks.size() - 1 &&
                  query_peak_mass - fragment_mz_tolerance > candidate_peaks[candidate_peak_index].getMass())
            {
                candidate_peak_index++;
            }

            // match the peaks within the fragment mass window if possible
            for(unsigned int index = 0; candidate_peak_index + index < candidate_peaks.size() &&
                fabs(query_peak_mass - candidate_peaks[candidate_peak_index + index].getMass()) <= fragment_mz_tolerance; index++)
            {
                bool can_match = false;
                // unshifted peak
                if(candidate_peaks[candidate_peak_index + index].getCharge() == 0)
                {
                    can_match = true;
                }
                // annotated peak shifted according to the peak's charge
                else if(query_peak_charge == candidate_peaks[candidate_peak_index + index].getCharge() &&
                        query_peak_charge != 0 && candidate_peaks[candidate_peak_index + index].getCharge() != 0)
                {
                    can_match = true;
                }
                // non-annotated peak shifted according to some charge
                else if(query_peak_charge == 0)
                {
                    can_match = true;
                }

                if(can_match)
                {
                    peak_matches.push_back(std::make_tuple(query_peak_intensity * candidate_peaks[candidate_peak_index + index].getIntensity(),
                                                           query_peak_index, candidate_peaks[candidate_peak_index + index].getIndex()));
                }
            }
        }

        candidate_peaks.clear();

        // use the most prominent peak matches to compute the score (sort in descending order)
        std::sort(peak_matches.begin(), peak_matches.end(), [](auto &peak_match1, auto &peak_match2) {
                  return std::get<0>(peak_match1) > std::get<0>(peak_match2); });
        SpectrumSpectrumMatch *this_match = new SpectrumSpectrumMatch(candidate_index);
        std::vector<bool> query_peaks_used(query->getNumPeaks(), false);
        std::vector<bool> candidate_peaks_used(candidate->getNumPeaks(), false);
        for(unsigned int peak_match_index = 0; peak_match_index < peak_matches.size(); peak_match_index++)
        {
            std::tuple<double, unsigned int, unsigned int> peak_match = peak_matches[peak_match_index];
            unsigned int query_peak_index = std::get<1>(peak_match);
            unsigned int candidate_peak_index = std::get<2>(peak_match);
            if(query_peaks_used[query_peak_index] == false && candidate_peaks_used[candidate_peak_index] == false)
            {
                this_match->setScore(this_match->getScore() + std::get<0>(peak_match));
                // save the matched peaks
                this_match->addPeakMatch(query_peak_index, candidate_peak_index);
                // make sure these peaks are not used anymore
                query_peaks_used[query_peak_index] = true;
                candidate_peaks_used[candidate_peak_index] = true;
            }
        }

        query_peaks_used.clear();
        candidate_peaks_used.clear();
        peak_matches.clear();

        // retain the match with the highest score
        if(best_match == NULL || best_match->getScore() < this_match->getScore())
        {
            if(best_match != NULL)
            {
                delete best_match;
            }
            best_match = this_match;
        }
        else
        {
            delete this_match;
        }
    }

    return best_match;
}
