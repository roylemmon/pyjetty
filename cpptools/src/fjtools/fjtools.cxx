#include "fjtools.hh"

#include <TF1.h>
#include <TH2F.h>
#include <THn.h>
#include <TMath.h>
#include <TString.h>
#include <TRandom.h>
#include <iostream>

namespace PyJettyFJTools
{
	// fraction of pT of jet j1 contained in j0 - constit by constit
	double matched_pt(const fastjet::PseudoJet &j0, const fastjet::PseudoJet &j1)
	{
		double pt_sum = 0;
		double pt_sum_1 = 0;
		for (auto &p1 : j1.constituents())
			pt_sum_1 += p1.pt();
		for (auto &p0 : j0.constituents())
		{
			for (auto &p1 : j1.constituents())
			{
				if (p0.user_index() == p1.user_index())
				{
					pt_sum += p1.pt();
				}
			}
		}
		return (pt_sum / pt_sum_1);
	}

	// return indices of jets matched to j jet	
	std::vector<int> matched_Ry(const fastjet::PseudoJet &j, const std::vector<fastjet::PseudoJet> &v, double Rmatch)
	{
		std::vector<int> retv;
		for(std::size_t i = 0; i < v.size(); ++i)
		{
			if (j.delta_R(v[i]) < Rmatch)
			{
				retv.push_back(i);
			}
		}
		return retv;
	}

	// return indices of jets matched to j jet	
	std::vector<int> matched_Reta(const fastjet::PseudoJet &j, const std::vector<fastjet::PseudoJet> &v, double Rmatch)
	{
		std::vector<int> retv;
		for(std::size_t i = 0; i < v.size(); ++i)
		{
			double dR = TMath::Sqrt(TMath::Power(j.delta_phi_to(v[i]), 2.) + TMath::Power(j.eta() - v[i].eta(), 2.));
			if (dR < Rmatch)
			{
				retv.push_back(i);
			}
		}
		return retv;
	}

    // Rebin 2D histogram h with name hname using axes given by x_bins and y_bins
    // If move_y_underflow is set, y-underflow bins in h_to_rebin are added to (x, 1) bin
    //     [WARNING: it is NOT preserved in y-underflow bin: it is really added to (x, 1)]
    // RETURNS: pointer to a new rebinned TH2F on the heap
    TH2F* rebin_th2(TH2F* h_to_rebin, std::string hname, int n_x_bins, double* x_bins,
                    int n_y_bins, double* y_bins, bool move_y_underflow /*= false*/) {

        // Initialize the new empty histogram
        std::string name = hname + "_rebinned";
        TH2F* h = new TH2F(name.c_str(), name.c_str(), n_x_bins, x_bins, n_y_bins, y_bins);

        /*  Probably don't need this check
        // Check whether sumw2 has been previously set, just for our info
        if (h->GetSumw2() == 0) {
            std::cout << "rebin_th2() -- sumw2 has not been set" << std::endl;
        } else {
            std::cout << "rebin_th2() -- sumw2 has been set" << std::endl;
        }  // if (h->GetSumw2() == 0)
        */

        // Loop over all bins and fill rebinned histogram
        for (unsigned int bin_x = 1; bin_x <= h_to_rebin->GetNbinsX(); bin_x++) {
            for (unsigned int bin_y = 0; bin_y <= h_to_rebin->GetNbinsY(); bin_y++) {

                // If underflow bin of observable, and if use_underflow is activated,
                //   put the contents of the underflow bin into the first bin of the rebinned TH2
                if (bin_y == 0) {
                    if (move_y_underflow) {
                        h->Fill(h_to_rebin->GetXaxis()->GetBinCenter(bin_x),
                                h->GetYaxis()->GetBinCenter(1),
                                h_to_rebin->GetBinContent(bin_x, bin_y));
                    }
                    continue;
                }  // biny == 0

                h->Fill(h_to_rebin->GetXaxis()->GetBinCenter(bin_x),
                        h_to_rebin->GetYaxis()->GetBinCenter(bin_y),
                        h_to_rebin->GetBinContent(bin_x, bin_y));

            }  // for(biny)
        }  // for(binx)

        // We need to manually set the uncertainties, since sumw2 does the wrong thing in this case
        // Specifically: We fill the rebinned histo from several separate weighted fills, so sumw2
        // gives sqrt(a^2+b^2) where we simply want counting uncertainties of sqrt(a+b).
        for (unsigned int i = 0; i <= h->GetNcells(); i++) {
            h->SetBinError(i, std::sqrt(h->GetBinContent(i)));
        }

        // We want to make sure sumw2 is set after rebinning, since
        // we will scale etc. this histogram
        if (h->GetSumw2() == 0) {
            h->Sumw2();
        }

        return h;

    }  // rebin_th2

	double boltzmann_norm(double *x, double *par)
	{
		// double fval = par[1] / x[0] * x[0] * TMath::Exp(-(2. / par[0]) * x[0]);
		// double fval = par[1] / x[0] * x[0] * TMath::Exp(-(x[0] / par[0]));
		// return fval;
		return par[1] * x[0] * TMath::Exp(-(x[0] / (par[0]/2.)));
	}

	double boltzmann(double *x, double *par)
	{
		return x[0] * TMath::Exp(-(x[0] / (par[0]/2.)));
	}

	std::string BoltzmannBackground::description()
	{
		std::string s;
		s = TString::Format("\n[i] BoltzmannBackground with \n    fmean_pt=%f\n    fmin_pt=%f\n    fmax_pt=%f\n", fmean_pt, fmin_pt, fmax_pt);
		return s;
	}

	BoltzmannBackground::BoltzmannBackground()
	: funbg(0)
	, fmean_pt(-1)
	, fmin_pt(-1)
	, fmax_pt(-1)
	, fparts()
	{
		reset(0.7, 0.15, 5.);
	}

	BoltzmannBackground::BoltzmannBackground(double mean_pt, double min_pt, double max_pt)
	: funbg(0)
	, fmean_pt(-1)
	, fmin_pt(-1)
	, fmax_pt(-1)
	, fparts()
	{
		reset(mean_pt, min_pt, max_pt);
	}


	void BoltzmannBackground::reset(double mean_pt, double min_pt, double max_pt)
	{
		bool _reinit = false;
		if (fmin_pt != min_pt)
		{
			_reinit = true;
			fmin_pt = min_pt;
		}
		if (fmax_pt != max_pt)
		{
			_reinit = true;
			fmax_pt = max_pt;
		}
		if (fmean_pt != mean_pt)
		{
			_reinit = true;
			fmean_pt = mean_pt;
		}
		if (_reinit)
		{
			TF1 _tmpf("_tmpf", boltzmann, fmin_pt, fmax_pt, 1);
			_tmpf.SetParameter(0, fmean_pt);
			double _int = _tmpf.Integral(fmin_pt, fmax_pt);
			if (_int == 0)
				_int = 1e-9;
			if (funbg)
			{
				delete funbg;
			}
			funbg = new TF1("BoltzmannBackground_funBG", boltzmann_norm, fmin_pt, fmax_pt, 2);
			funbg->SetParameter(0, fmean_pt);
			funbg->SetParameter(1, 1./_int);		
		}
		fparts.clear();
	}

	double BoltzmannBackground::eval(double x)
	{
		return funbg->Eval(x);
	}

	double BoltzmannBackground::integral()
	{
		return funbg->Integral(fmin_pt, fmax_pt);
	}

	std::string BoltzmannBackground::get_formula()
	{
		std::string s;
		s = TString::Format("%f * x[0] * TMath::Exp(-(x[0] / %f))", funbg->GetParameter(1), funbg->GetParameter(0)).Data();
		return s;
	}

	double BoltzmannBackground::get_constant()
	{
		return funbg->GetParameter(1);
	}

	// getNrandom particles w/ indexoffset ...
	std::vector<fastjet::PseudoJet> BoltzmannBackground::generate(int nparts, double max_eta, int offset)
	{	
		fparts.clear();
		for (int i = 0; i < nparts; i++)
		{
			double _pt  = funbg->GetRandom(fmin_pt, fmax_pt);
			double _eta = gRandom->Rndm() * max_eta * 2. - max_eta;
			double _phi = gRandom->Rndm() * TMath::Pi() * 2. - TMath::Pi();
			fastjet::PseudoJet _p;
			_p.reset_PtYPhiM (_pt, _eta, _phi, 0.0);
			_p.set_user_index(i + offset);
			fparts.push_back(_p);		
		}
		return fparts;
	}

	// subtract particles (according to the probability... - fixed to 1 in maxpt range)
	std::vector<fastjet::PseudoJet> BoltzmannBackground::subtract(const std::vector<fastjet::PseudoJet> &v, double mean_pt, int n)
	{
		fparts.clear();
		double _count = n;
		double _mean_pt = mean_pt;
		double _total_pt = _count * _mean_pt;
		if (_mean_pt < 0 || _count < 0)
		{
			for (auto &p : v)
			{
				if (p.pt() <= fmax_pt)
				{
					_total_pt += p.pt();
					_count += 1.;
				}
			}
			if (_count > 0)
			{
				_mean_pt = _total_pt / _count;
			}
		}
		if (_count > 0)
			reset(_mean_pt, fmin_pt, fmax_pt);
		std::cout << "* total_pt=" << TString::Format("%f", _total_pt).Data() << std::endl;
		for (auto &p : v)
		{
			if (p.pt() > fmax_pt)
			{
				fastjet::PseudoJet _p(p.px(), p.py(), p.pz(), p.e());
				_p.set_user_index(p.user_index());
				fparts.push_back(_p);		
			}
			else
			{
				// std::cout << TString::Format("%f", fmin_pt).Data() << std::endl;
				// if (p.pt() > fmin_pt)
				// 	std::cout << TString::Format("%f %f %f", p.pt(), funbg->Eval(p.pt()), _count).Data() << std::endl;
				// double fraction = funbg->Eval(p.pt()) * funbg->GetNpx() / 2.;
				// double fraction = funbg->Eval(p.pt()) * (fmax_pt - fmean_pt) * funbg->GetNpx();
				double fraction = funbg->Eval(p.pt()) * _total_pt;
				if ((p.pt() - fraction) > fmin_pt)
				{
					fastjet::PseudoJet _p;
					_p.reset_PtYPhiM(p.pt() - fraction, p.rap(), p.phi(), p.m());
					// std::cout << TString::Format("%f - %f = %f", p.pt(), fraction, _p.pt()).Data() << std::endl;
					_p.set_user_index(p.user_index());
					fparts.push_back(_p);		
				}
			}
		}
		return fparts;
	}

	// subtract particles (according to the probability... - fixed to 1 in maxpt range)
	std::vector<fastjet::PseudoJet> BoltzmannBackground::subtract_recalc_from_vector(const std::vector<fastjet::PseudoJet> &v)
	{
		fparts.clear();
		double _total_pt = 0;
		double _count = 0;
		for (auto &p : v)
		{
			if (p.pt() <= fmax_pt)
			{
				_total_pt += p.pt();
				_count += 1.;
			}
		}
		double _mean_pt = 0;
		if (_count > 0)
		{
			_mean_pt = _total_pt / _count;
			reset(_mean_pt, fmin_pt, fmax_pt);
		}
		for (auto &p : v)
		{
			if (p.pt() > fmax_pt)
			{
				fastjet::PseudoJet _p(p.px(), p.py(), p.pz(), p.e());
				_p.set_user_index(p.user_index());
				fparts.push_back(_p);		
			}
			else
			{
				// std::cout << TString::Format("%f", fmin_pt).Data() << std::endl;
				// if (p.pt() > fmin_pt)
				// 	std::cout << TString::Format("%f %f %f", p.pt(), funbg->Eval(p.pt()), _count).Data() << std::endl;
				// double fraction = funbg->Eval(p.pt()) * funbg->GetNpx() / 2.;
				// double fraction = funbg->Eval(p.pt()) * (fmax_pt - fmean_pt) * funbg->GetNpx();
				// std::cout << TString::Format("%f", _total_pt).Data() << std::endl;
				double fraction = funbg->Eval(p.pt()) * _total_pt;
				if ((p.pt() - fraction) > fmin_pt)
				{
					fastjet::PseudoJet _p;
					_p.reset_PtYPhiM(p.pt() - fraction, p.rap(), p.phi(), p.m());
					// std::cout << TString::Format("%f - %f = %f", p.pt(), fraction, _p.pt()).Data() << std::endl;
					_p.set_user_index(p.user_index());
					fparts.push_back(_p);		
				}
			}
		}
		return fparts;
	}

	BoltzmannBackground::~BoltzmannBackground()
	{
		delete funbg;
	}
}
