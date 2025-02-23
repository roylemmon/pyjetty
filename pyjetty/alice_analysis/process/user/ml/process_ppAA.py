#!/usr/bin/env python3

"""
Class to read pp vs AA data set and compute N-subjettiness
"""

import os
import sys
import argparse
import yaml
import h5py

# Data analysis and plotting
import pandas as pd
import numpy as np
from matplotlib import pyplot as plt

# Fastjet via python (from external library heppy)
import fastjet as fj
import fjcontrib
import fjext

# Energy flow package
import energyflow

# Base class
from pyjetty.alice_analysis.process.base import common_base

################################################################
class ProcessppAA(common_base.CommonBase):

    #---------------------------------------------------------------
    # Constructor
    #---------------------------------------------------------------
    def __init__(self, config_file='', input_file='', output_dir='', **kwargs):
        super(common_base.CommonBase, self).__init__(**kwargs)
        
        self.config_file = config_file
        self.output_dir = output_dir
        if not os.path.exists(self.output_dir):
            os.makedirs(self.output_dir)
        
        # Initialize config file
        self.initialize_config()
        
        # Load reformatted pp and AA jets from rstorage
        # X : a three-dimensional numpy array of jets:
        #     list of jets with list of particles for each jet, with (E,px,py,pz) - not massless, no PID!
        # y : a numpy array of pp/AA jet labels (pp=0 and AA=1).
        # The jets are padded with zero-particles in order to make a contiguous array.
        print()
        print('Loading pp vs. AA dataset:')
        with h5py.File(input_file,'r') as hdf:
            ppAA_jets = hdf.get('data')[:self.train + self.val + self.test]
            self.X = np.array(ppAA_jets)
            ppAA_labels = hdf.get('labels')[:self.train + self.val + self.test]
            self.y = np.array(ppAA_labels)
            print('(n_jets, n_particles per jet, n_variables): {}'.format(self.X.shape))
        print()

        # Next, we will transform these into fastjet::PseudoJet objects.
        # This allows us to use the fastjet contrib to compute Nsubjettiness, and in general it
        # will be needed in order to perform jet finding on an event (in data or MC).

        # Translate 3D numpy array (62532 jets x 800 particles x 4 variables) into a dataframe
        # Define a unique index for each jet
        # Note: for q/g data set, the 4 variables were (pT,y,phi,PID), now (E,px,py,pz)
        columns = ['E', 'px', 'py', 'pz']
        df_particles = pd.DataFrame(self.X.reshape(-1, 4), columns=columns)
        df_particles.index = np.repeat(np.arange(self.X.shape[0]), self.X.shape[1]) + 1
        df_particles.index.name = 'jet_id'
        
        # (i) Group the particle dataframe by jet id
        #     df_particles_grouped is a DataFrameGroupBy object with one particle dataframe per jet
        df_particles_grouped = df_particles.groupby('jet_id')
        
        # (ii) Transform the DataFrameGroupBy object to a SeriesGroupBy of fastjet::PseudoJets
        print('Converting particle dataframe to fastjet::PseudoJets...')
        self.df_fjparticles = df_particles_grouped.apply(self.get_fjparticles)
        print('Done.')
        print()
        
        # Create list of N-subjettiness observables: number of axes and beta values
        self.N_list = []
        self.beta_list = []
        for i in range(self.K-2):
            self.N_list += [i+1] * 3
            self.beta_list += [0.5,1,2]
        self.N_list += [self.K-1] * 2  
        self.beta_list += [1,2]
        
        # Construct dictionary to store all jet quantities of interest
        self.jet_variables = {}
        for i,N in enumerate(self.N_list):
            beta = self.beta_list[i]
            self.jet_variables['n_subjettiness_N{}_beta{}'.format(N,beta)] = []
        
        print(self)
        print()
        
    #---------------------------------------------------------------
    # Initialize config file into class members
    #---------------------------------------------------------------
    def initialize_config(self):
    
        # Read config file
        with open(self.config_file, 'r') as stream:
          config = yaml.safe_load(stream)
          
        self.train = config['n_train']
        self.val = config['n_val']
        self.test = config['n_test']
        self.K = max(config['K'])

    #---------------------------------------------------------------
    # Main processing function
    #---------------------------------------------------------------
    def process_ppAA(self):
    
        # Loop over jets and compute quantities of interest
        # Fill each of the jet_variables into a list
        fj.ClusterSequence.print_banner()
        print('Finding jets and computing N-subjettiness...')
        result = [self.analyze_event(fj_particles) for fj_particles in self.df_fjparticles]
        
        # Transform the dictionary of lists into a dictionary of numpy arrays
        self.jet_variables_numpy = self.transform_to_numpy(self.jet_variables)
        n_subjettiness = self.jet_variables_numpy['n_subjettiness_N{}_beta{}'.format(self.N_list[0], self.beta_list[0])]
        print('Done. Number of clustered jets: {}'.format(len(n_subjettiness)))
        print()
        
        # Reformat output for ML algorithms (array with 1 array per jet which contain all N-subjettiness values)
        X_Nsub = np.array([list(self.jet_variables_numpy.values())])[0].T
        
        # Write jet arrays to file
        with h5py.File(os.path.join(self.output_dir, 'nsubjettiness.h5'), 'w') as hf:
            hf.create_dataset('y', data=self.y)
            hf.create_dataset('X', data=self.X)
            hf.create_dataset('X_Nsub', data=X_Nsub)
            hf.create_dataset('N_list', data=self.N_list)
            hf.create_dataset('beta_list', data=self.beta_list)
                        
        # Plot jet quantities
        if self.K <= 6:
            self.plot_nsubjettiness()
        
    #---------------------------------------------------------------
    # Process an event (in this case, just a single jet per event)
    #---------------------------------------------------------------
    def analyze_event(self, fj_particles):
        
        # Cluster each jet with R=infinity
        jetR = fj.JetDefinition.max_allowable_R
        jet_def = fj.JetDefinition(fj.antikt_algorithm, jetR)
        cs = fj.ClusterSequence(fj_particles, jet_def)
        jet = fj.sorted_by_pt(cs.inclusive_jets())[0]

        # Compute N-subjettiness
        axis_definition = fjcontrib.KT_Axes()
        for i,N in enumerate(self.N_list):
            beta = self.beta_list[i]
        
            measure_definition = fjcontrib.UnnormalizedMeasure(beta)
            n_subjettiness_calculator = fjcontrib.Nsubjettiness(N, axis_definition, measure_definition)
            n_subjettiness = n_subjettiness_calculator.result(jet)/jet.pt()
            self.jet_variables['n_subjettiness_N{}_beta{}'.format(N, beta)].append(n_subjettiness)
        
        # Compute four-vector...
        # ...
            
    #---------------------------------------------------------------
    # Plot N-subjettiness
    #---------------------------------------------------------------
    def plot_nsubjettiness(self):
    
        linestyles = ['-', '--', ':', '-.', '-']
    
        bins = np.linspace(0, 0.7, 100)
        for i,N in enumerate(self.N_list):
            beta = self.beta_list[i]
            
            plt.hist(self.jet_variables_numpy['n_subjettiness_N{}_beta{}'.format(N,beta)],
                     bins,
                     histtype='stepfilled',
                     label = r'$N={}, \beta={}$'.format(N, beta),
                     linewidth=2,
                     linestyle=linestyles[N-1],
                     alpha=0.5)
                     
        plt.xlabel(r'$\tau_{N}^{\beta}$', fontsize=14)
        plt.yscale('log')
        legend = plt.legend(loc='best', fontsize=10, frameon=False)
        plt.tight_layout()
        plt.savefig(os.path.join(self.output_dir, 'Nsubjettiness.pdf'))
        plt.close()
          
    #---------------------------------------------------------------
    # Transform dictionary of lists into a dictionary of numpy arrays
    #---------------------------------------------------------------
    def transform_to_numpy(self, jet_variables_list):

        jet_variables_numpy = {}
        for key,val in jet_variables_list.items():
            jet_variables_numpy[key] = np.array(val)
                    
        return jet_variables_numpy
            
    #---------------------------------------------------------------
    # Cluster jets
    #---------------------------------------------------------------
    def get_fjparticles(self, df_particles_grouped):
                                                 
        user_index_offset = 0
        return fjext.vectorize_px_py_pz_e(df_particles_grouped['px'].values,
                                          df_particles_grouped['py'].values,
                                          df_particles_grouped['pz'].values,
                                          df_particles_grouped['E'].values,
                                          user_index_offset)        

##################################################################
if __name__ == '__main__':

    # Define arguments
    parser = argparse.ArgumentParser(description='Process pp AA')
    parser.add_argument('-c', '--configFile', action='store',
                        type=str, metavar='configFile',
                        default='./config/ml/ppAA.yaml',
                        help='Path of config file for analysis')
    parser.add_argument('-f', '--inputFile', action='store',
                        type=str, metavar='inputFile',
                default='./skim_blah.h5',
                        help='Path of input file for analysis')
    parser.add_argument('-o', '--outputDir', action='store',
                        type=str, metavar='outputDir',
                        default='./TestOutput',
                        help='Output directory for output to be written to')

    # Parse the arguments
    args = parser.parse_args()

    print('Configuring...')
    print('configFile: \'{0}\''.format(args.configFile))
    print('inputFile: \'{0}\''.format(args.inputFile))
    print('ouputDir: \'{0}\"'.format(args.outputDir))

    # If invalid configFile is given, exit
    if not os.path.exists(args.configFile):
        print('File \"{0}\" does not exist! Exiting!'.format(args.configFile))
        sys.exit(0)

    # If invalid inputFile is given, exit
    if not os.path.exists(args.inputFile):
        print('File \"{0}\" does not exist! Exiting!'.format(args.inputFile))
        sys.exit(0)

    analysis = ProcessppAA(config_file=args.configFile, input_file=args.inputFile, output_dir=args.outputDir)
    analysis.process_ppAA()
