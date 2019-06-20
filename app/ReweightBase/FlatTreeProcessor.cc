#include <iostream>
#include <string>
#include "TTree.h"
#include "TFile.h"
#include "TH1F.h"

#include "G4ReweightTraj.hh"
#include "G4ReweightStep.hh"
#include "G4Reweighter.hh"

#include "G4ReweightParameterMaker.hh"

#include "fhiclcpp/make_ParameterSet.h"
#include "fhiclcpp/ParameterSet.h"

#ifdef FNAL_FHICL
#include "cetlib/filepath_maker.h"
#endif

//Defines parseArgs and the command line options
#include "parse_reweight_args.hh"

void ProcessFlatTree(std::string &inFileName, std::string &outFileName, G4Reweighter &theReweighter );

int main(int argc, char ** argv){

  if(!parseArgs(argc, argv)) 
    return 0;

  fhicl::ParameterSet pset;
  #ifdef FNAL_FHICL
    // Configuration file lookup policy.
    char const* fhicl_env = getenv("FHICL_FILE_PATH");
    std::string search_path;

    if (fhicl_env == nullptr) {
      std::cerr << "Expected environment variable FHICL_FILE_PATH is missing or empty: using \".\"\n";
      search_path = ".";
    }
    else {
      search_path = std::string{fhicl_env};
    }

    cet::filepath_first_absolute_or_lookup_with_dot lookupPolicy{search_path};

    fhicl::make_ParameterSet(fcl_file, lookupPolicy, pset);

  #else
    pset = fhicl::make_ParameterSet(fcl_file);
  #endif

  std::string outFileName = pset.get< std::string >( "OutputFile" );
  if( output_file_override  != "empty" ){
    outFileName = output_file_override;
  }

  std::string inFileName  = pset.get< std::string >( "InputFile" ); 
  if( input_file_override  != "empty" ){
    inFileName = input_file_override;
  }

  std::string FracsFileName = pset.get< std::string >( "Fracs" );
  TFile FracsFile( FracsFileName.c_str(), "OPEN" );

  std::vector< fhicl::ParameterSet > FitParSets = pset.get< std::vector< fhicl::ParameterSet > >("ParameterSet");

  try{
    G4ReweightParameterMaker ParMaker( FitParSets );

    G4Reweighter * theReweighter = new G4Reweighter( &FracsFile, ParMaker.GetFSHists() ); 
    if( enablePiMinus ) theReweighter->SetPiMinus();

    std::string XSecFileName = pset.get< std::string >( "XSec" );
    TFile XSecFile( XSecFileName.c_str(), "OPEN" );

    theReweighter->SetTotalGraph(&XSecFile);

  
    ProcessFlatTree(inFileName, outFileName, *theReweighter);

    std::cout << "done" << std::endl;
  }
  catch( const std::exception &e ){
    std::cout << "Caught exception" << std::endl;
  }

  return 0;
}

void ProcessFlatTree( std::string &inFileName, std::string &outFileName, G4Reweighter &theReweighter ){
  
  //Input variables
  int input_event;
  std::vector< double > * input_X  = new std::vector< double >();
  std::vector< double > * input_Y  = new std::vector< double >();
  std::vector< double > * input_Z  = new std::vector< double >();
  std::vector< double > * input_PX = new std::vector< double >();
  std::vector< double > * input_PY = new std::vector< double >();
  std::vector< double > * input_PZ = new std::vector< double >();
  int input_ID, input_PDG;
  std::string *input_EndProcess = new std::string();

  //Output variables
  int output_event;
  double weight;
  double track_length;
  //////////////////



  //Get input tree and set branches
  TFile inputFile( inFileName.c_str(), "OPEN" );
  TTree * inputTree = (TTree*)inputFile.Get("flattree/FlatTree");
  inputTree->SetBranchAddress( "event",         &input_event );
  inputTree->SetBranchAddress( "true_beam_X",   &input_X );
  inputTree->SetBranchAddress( "true_beam_Y",   &input_Y );
  inputTree->SetBranchAddress( "true_beam_Z",   &input_Z );
  inputTree->SetBranchAddress( "true_beam_PX",  &input_PX );
  inputTree->SetBranchAddress( "true_beam_PY",  &input_PY );
  inputTree->SetBranchAddress( "true_beam_PZ",  &input_PZ );
  inputTree->SetBranchAddress( "true_beam_PDG", &input_PDG );
  inputTree->SetBranchAddress( "true_beam_ID",  &input_ID );
  inputTree->SetBranchAddress( "true_beam_EndProcess", &input_EndProcess );


  //Create output
  TFile outputFile( outFileName.c_str(), "RECREATE" );
  TTree outputTree = TTree( "output", "" );
  outputTree.Branch( "event",        &output_event );
  outputTree.Branch( "weight",       &weight );
  outputTree.Branch( "track_length", &track_length );

  for( size_t i = 0; i < inputTree->GetEntries(); ++i ){
    inputTree->GetEvent(i);

    //Make a G4ReweightTraj -- This is the reweightable object
    G4ReweightTraj theTraj(input_ID, input_PDG, 0, input_event, std::make_pair(0,0));

    //Create its set of G4ReweightSteps and add them to the Traj
    std::vector< G4ReweightStep* > allSteps;
    for( size_t i = 1; i < input_PX->size(); ++i ){

      std::string proc = "default";
      if( i == input_PX->size() - 1 )
        proc = *input_EndProcess; 
      
      double len = sqrt(
        std::pow( ( input_X->at(i-1) - input_X->at(i) ), 2 )  + 
        std::pow( ( input_Y->at(i-1) - input_Y->at(i) ), 2 )  + 
        std::pow( ( input_Z->at(i-1) - input_Z->at(i) ), 2 )   
      );

      double preStepP[3] = { 
        input_PX->at(i-1),
        input_PY->at(i-1),
        input_PZ->at(i-1)
      };

      double postStepP[3] = { 
        input_PX->at(i),
        input_PY->at(i),
        input_PZ->at(i)
      };

      G4ReweightStep * theStep = new G4ReweightStep( input_ID, input_PDG, 0, input_event, preStepP, postStepP, len, proc ); 
      theTraj.AddStep( theStep );
    }

    //Get the weight from the G4ReweightTraj
    weight = theReweighter.GetWeight( &theTraj );
    track_length = theTraj.GetTotalLength();
    output_event = input_event;

    outputTree.Fill();
  }
  
  outputFile.cd();
  outputTree.Write();
  outputFile.Close();
}
