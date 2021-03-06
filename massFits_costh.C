#include "TTree.h"
#include "TFile.h"
#include "TCanvas.h"

#include "RooDataSet.h"
#include "RooRealVar.h"
#include "RooArgList.h"
// #include "RooPolynomial.h"
#include "RooChebychev.h"
#include "RooWorkspace.h"
#include "RooFitResult.h"
#include "RooPlot.h"
#include "RooCBShape.h"

#include <string>
// #include <vector>
#include <iostream>
#include <array>
#include <sstream>
#include <regex>

void buildModel(RooWorkspace* ws)
{
  RooRealVar* mass = ws->var("mass");

  mass->setRange("fitRange", 8.6, 11.4);

  RooRealVar a0("a0", "a0", 0.5, -1, 1); a0.setConstant(false);
  RooRealVar a1("a1", "a1", 0, -1, 1); a1.setConstant(false);
  RooRealVar a2("a2", "a2", 0, -1, 1); a2.setConstant(false);
  RooChebychev bkgPoly("bkgPoly", "polynomial background", *mass, RooArgList(a0, a1, a2));

  constexpr double mPDG1S = 9.460;
  constexpr double mPDG2S = 10.023;
  constexpr double mPDG3S = 10.355;

  RooRealVar r2S1S("r2S1S", "r2S1S", mPDG2S / mPDG1S);
  RooRealVar r3S1S("r3S1S", "r3S1S", mPDG3S / mPDG1S);

  RooRealVar mean1S("mean1S", "mean1S", mPDG1S, 8.6, 11.4);
  RooRealVar sigma1S("sigma1S", "sigma1S", 0.1, 0, 2.5);
  RooRealVar alpha("alpha", "alpha", 1.33, 0, 2.5);
  RooRealVar n("n", "n", 6.6, 0, 10);

  RooFormulaVar mean2S("mean2S", "mean2S", "mean1S * r2S1S", RooArgList(mean1S, r2S1S));
  RooFormulaVar sigma2S("sigma2S", "sigma2S", "sigma1S * r2S1S", RooArgList(sigma1S, r2S1S));

  RooFormulaVar mean3S("mean3S", "mean3S", "mean1S * r3S1S", RooArgList(mean1S, r3S1S));
  RooFormulaVar sigma3S("sigma3S", "sigma3S", "sigma1S * r3S1S", RooArgList(sigma1S, r3S1S));

  RooCBShape sigCB1S("sigCB1S", "sigCB1S", *mass, mean1S, sigma1S, alpha, n);
  RooCBShape sigCB2S("sigCB2S", "sigCB2S", *mass, mean2S, sigma2S, alpha, n);
  RooCBShape sigCB3S("sigCB3S", "sigCB3S", *mass, mean3S, sigma3S, alpha, n);

  ws->import(RooArgList(bkgPoly, sigCB1S, sigCB2S, sigCB3S));
  ws->factory("SUM:fullModel(fBkg[0.5,0,1] * bkgPoly, f1S[0.2,0,1]*sigCB1S, f2S[0.15,0,1]*sigCB2S, sigCB3S)");
}

void plotModel(RooWorkspace* ws, const std::string& snapshot)
{
  using namespace RooFit;

  auto* mass = ws->var("mass");
  auto* frame = mass->frame(Range("fitRange"));
  auto* data = ws->data("fullData");
  auto* fullModel = ws->pdf("fullModel");

  ws->loadSnapshot("snap_fullData");

  data->plotOn(frame);
  fullModel->plotOn(frame);

  auto* can = new TCanvas("c", "c", 1000, 1000);
  can->cd();
  frame->Draw();

  can->SaveAs("fitResults.pdf");
}

template<typename V>
std::string getBinExpr(const V& binning, const size_t bin, const std::string& var)
{
  std::stringstream sstr;
  sstr << "(" << var << " > " << binning[bin - 1] << " && " << var << " < " << binning[bin] << ")";
  return sstr.str();
}

template<typename V>
std::string getBinName(const V& binning, const size_t bin, const std::string& var)
{
  std::stringstream sstr;
  sstr << var << "_" << binning[bin - 1] << "to" << binning[bin];

  return std::regex_replace(sstr.str(), std::regex("([0-9]+)\\.([0-9]+)"), "$1p$2");
}

// NOTE: at the moment only supports > than
std::string getCutString(const std::string var, const double val)
{
  std::stringstream sstr;
  sstr << var << " > " << val;
  return sstr.str();
}

std::string getCutName(const std::string var, const double val)
{
  std::stringstream sstr;
  sstr << var << "_" << val;

  return std::regex_replace(sstr.str(), std::regex("([0-9]+).([0-9]+)"), "$1p$2");
}

void doFit(RooWorkspace* ws, const std::string& cut, const std::string& name,
           const std::string& fullDataName)
{
  using namespace RooFit;

  auto* fullData = ws->data(fullDataName.c_str());
  auto* model = ws->pdf("fullModel");
  auto* params = (RooArgSet*) model->getParameters(*(ws->var("mass")));

  auto* binData = fullData->reduce(cut.c_str());
  binData->SetName(("data_" + name).c_str());
  ws->import(*binData);

  auto* rlt = model->fitTo(*binData, Minos(false), NumCPU(4), Range("fitRange"),
                           Save(true));

  rlt->SetName(("fitResults_" + name).c_str());
  ws->import(*rlt);

  ws->saveSnapshot(("snap_" + name).c_str(), *params, true);
}


void costhBinFits(RooWorkspace* ws, const std::string&& fullDataName)
{
  constexpr std::array<double, 10> absCosThEdges = {0.0, 0.1, 0.2, 0.3, 0.4,
                                                    0.5, 0.6, 0.7, 0.8, 1.0};

  for (size_t i = 1; i < absCosThEdges.size(); ++i) {
    const auto cutString = getBinExpr(absCosThEdges, i, "TMath::Abs(costh_HX)");
    const auto binName = getBinName(absCosThEdges, i, "absCosth");

    doFit(ws, cutString, binName, fullDataName);
  }
}

void NchCutFits(RooWorkspace* ws, const std::string& fullDataName)
{
  constexpr std::array<double, 6> nchCuts = {2, 4, 5, 6, 8, 10};

  for (const double& cut : nchCuts) {
    const auto cutStr = getCutString("Nch", cut);
    const auto cutName = getCutName("Nch", cut);

    doFit(ws, cutStr, cutName, fullDataName);
  }
}

struct Cuts {
  Cuts(std::array<double, 2> p, std::array<double, 2> N) : pT(p), Nch(N) {}
  std::array<double, 2> pT;
  std::array<double, 2> Nch;

  std::string getCutString() const { return getBinExpr(Nch, 1, "Nch") + " && " + getBinExpr(pT, 1, "pT"); }
  std::string getCutName() const { return getBinName(Nch, 1, "Nch") + "_" + getBinName(pT, 1, "pT"); }
};

/** function to to some combinations of Nch and pT cuts. */
void NchPtCutsFits(RooWorkspace* ws, const std::string& fullDataName)
{
  const std::vector<Cuts> cuts = {Cuts({15, 70}, {0, 180}), // Suggestion 1 from Carlos
                                  Cuts({10, 70}, {23, 180}), // Suggestion 2 from Carlos
                                  Cuts({10, 70}, {20, 180}),
                                  Cuts({10, 15}, {0, 20}), // Suggestion 3 from Carlos
                                  Cuts({10, 12}, {0, 20}),
                                  Cuts({12, 15}, {0, 20}),
                                  Cuts({15, 70}, {20, 180}), // Suggestion 4 from Carlos
                                  Cuts({15, 70}, {0, 20}), // needed?
                                  Cuts({15, 70}, {0, 23}) // needed?
  };

  for (const auto& c : cuts) {
    const auto cutStr = c.getCutString();
    const auto cutName = c.getCutName();

    doFit(ws, cutStr, cutName, fullDataName);
  }
}


void massFits_costh(const std::string& fn)
{
  using namespace RooFit;

  TFile* f = TFile::Open(fn.c_str());
  TTree* t = static_cast<TTree*>(f->Get("selectedData"));

  RooRealVar pT("pT", "p_{T}", 10, 70);
  RooRealVar mass("mass", "m_{B}", 8.4, 11.6);
  RooRealVar Nch("Nch", "Nch", 0, 180);
  RooRealVar costh("costh_HX", "cos#theta^{HX}", -1, 1);
  RooRealVar phi("phi_HX", "phi^{HX}", -180, 180);
  RooRealVar ctau("ctau", "c#tau", -40, 40);
  RooRealVar ctauErr("ctauErr", "#sigma_{c#tau}", 0, 5);

  RooDataSet fullData("fullData", "dataset without cuts", t,
                      RooArgList(pT, mass, Nch, costh, phi, ctau, ctauErr));

  RooWorkspace* ws = new RooWorkspace("workspace", "workspace");
  ws->import(fullData);

  buildModel(ws);

  auto* model = ws->pdf("fullModel");
  auto* params = (RooArgSet*) model->getParameters(mass);


  // auto* fitData = dynamic_cast<RooDataSet*>(fullData.reduce("pT > 15.0"));
  // auto* fitData = dynamic_cast<RooDataSet*>(fullData.reduce("Nch < 75.0"));
  auto* fitData = &fullData;
  fitData->SetName("fitData");
  ws->import(*fitData);

  RooFitResult* rlt = model->fitTo(*fitData, Minos(false), NumCPU(4), Range("fitRange"),
                                   Save(true));

  ws->saveSnapshot("snap_fullData", *params, true);
  // std::cout << "First Fit done ==================================================\n";
  // RooFitResult* rlt = ws->pdf("fullModel")->fitTo(fullData, NumCPU(4), Save(true),
  //                                                 Minos(true), Range("fitRange"));

  std::cout << rlt->status() << " " << rlt->covQual() << "\n";
  ws->import(*rlt);

  // costhBinFits(ws, "fitData");
  // NchCutFits(ws, "fitData");
  // ws->writeToFile("ws_fit_result_Nch_cuts_Nch_lt75.root");

  NchPtCutsFits(ws, "fitData");
  ws->writeToFile("ws_fit_result_Nch_pT_combi_cuts.root");

  // plotModel(ws, "snap_fullData");
}

#if !(defined(__CINT__) || defined(__CLING__))
int main(int argc, char *argv[])
{
  const std::string filename = argv[1];

  massFits_costh(filename);

  return 0;
}
#endif
