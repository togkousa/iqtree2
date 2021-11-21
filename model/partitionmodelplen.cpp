//
//  partitionmodelplen.cpp
//  iqtree
//
//  Created by Olga on 04/05/17.
//
//

#include <stdio.h>
#include "model/partitionmodelplen.h"
#include "utils/timeutil.h"
#include "model/modelmarkov.h"

/**********************************************************
 * class PartitionModelPlen
 **********************************************************/

//const double MIN_GENE_RATE = 0.001;
//const double MAX_GENE_RATE = 1000.0;
//const double TOL_GENE_RATE = 0.0001;

PartitionModelPlen::PartitionModelPlen()
: PartitionModel()
{
    //    optimizing_part = -1;
}

PartitionModelPlen::PartitionModelPlen(Params &params, PhyloSuperTreePlen *tree,
                                       ModelsBlock *models_block,
                                       PhyloTree* report_to_tree)
    : PartitionModel(params, tree, models_block, report_to_tree)
{
    //    optimizing_part = -1;
}

PartitionModelPlen::~PartitionModelPlen()
{
}

void PartitionModelPlen::startCheckpoint() {
    checkpoint->startStruct("PartitionModelPlen");
}

void PartitionModelPlen::saveCheckpoint() {
    startCheckpoint();
    auto tree = dynamic_cast<PhyloSuperTreePlen*>(site_rate->getTree());
    ASSERT(tree!=nullptr);
    if (!tree->fixed_rates) {
        int nrates = static_cast<int>(tree->part_info.size());
        double *part_rates = new double[nrates];
        for (size_t i = 0; i < nrates; i++) {
            part_rates[i] = tree->part_info[i].part_rate;
        }
        CKP_ARRAY_SAVE(nrates, part_rates);
        delete [] part_rates;
    }
    endCheckpoint();
    PartitionModel::saveCheckpoint();
}

void PartitionModelPlen::restoreCheckpoint() {
    startCheckpoint();
    auto tree = dynamic_cast<PhyloSuperTreePlen*>(site_rate->getTree());
    ASSERT(tree!=nullptr);
    if (!tree->fixed_rates) {
        int nrates = static_cast<int>(tree->part_info.size());
        double *part_rates = new double[nrates];
        if (CKP_ARRAY_RESTORE(nrates, part_rates)) {
            for (size_t i = 0; i < nrates; i++) {
                tree->part_info[i].part_rate = part_rates[i];
            }
            tree->mapTrees();
        }
        delete [] part_rates;
    }
    endCheckpoint();
    PartitionModel::restoreCheckpoint();
}


double PartitionModelPlen::optimizeParameters(int fixed_len, bool write_info,
                                              double logl_epsilon, double gradient_epsilon,
                                              PhyloTree* report_to_tree) {
    auto tree = dynamic_cast<PhyloSuperTreePlen*>(site_rate->getTree());
    ASSERT(tree!=nullptr);
    if (report_to_tree==nullptr) {
        report_to_tree = tree;
    }
    double tree_lh = 0.0, cur_lh = 0.0;
    int    ntrees   = static_cast<int>(tree->size());
    
    //tree->initPartitionInfo(); // FOR OLGA: needed here

    unordered_map<string, bool> fixed_params;
    unordered_map<string, ModelSubst*>::iterator it;

    for(int part = 0; part < ntrees; part++){
        tree->part_info[part].cur_score = 0.0;
    }
    
//    if (fixed_len == BRLEN_OPTIMIZE) {
//        tree_lh = tree->optimizeAllBranches(1);
//    } else {
//        tree_lh = tree->computeLikelihood();
//    }
    tree_lh = tree->computeLikelihood();
    
    report_to_tree->hideProgress();
    cout<<"Initial log-likelihood: "<<tree_lh<<endl;
    report_to_tree->showProgress();
    double begin_time = getRealTime();
    int i;
    for(i = 1; i < tree->params->num_param_iterations; i++) {
        cur_lh = 0.0;
        if (tree->part_order.empty()) {
            tree->computePartitionOrder();
        }
        cur_lh = optimizeSubtreeModelParameters(tree, i, gradient_epsilon,
                                               report_to_tree);
        if (tree->params->link_alpha) {
            cur_lh = optimizeLinkedAlpha(write_info, gradient_epsilon);
        }
        // optimize linked models
        if (!linked_models.empty()) {
            double new_cur_lh = optimizeLinkedModels(write_info, gradient_epsilon,
                                                     report_to_tree);
            ASSERT(new_cur_lh > cur_lh - 0.1);
            cur_lh = new_cur_lh;
        }

        if (verbose_mode >= VerboseMode::VB_MED) {
            cout << "LnL after optimizing individual models: " << cur_lh << endl;
        }
        if (cur_lh <= tree_lh - 1.0) {
            // more info for ASSERTION
            writeInfo(cout);
            tree->printTree(cout, WT_BR_LEN+WT_NEWLINE);
        }
        ASSERT(cur_lh > tree_lh - 1.0 && "individual model opt reduces LnL");
        
        tree->clearAllPartialLH();

        cur_lh  = optimizeGeneRate(tree, tree_lh, cur_lh, 
                                   gradient_epsilon);
        
        tree_lh = optimizeBranchLengthsIfRequested(tree, fixed_len, i, cur_lh,
                                                   logl_epsilon, gradient_epsilon);
        
        report_to_tree->hideProgress();
        cout<<"Current log-likelihood at step " << i << ": " << cur_lh<<endl;
        report_to_tree->showProgress();
        if(fabs(cur_lh-tree_lh) < logl_epsilon) {
            tree_lh = cur_lh;
            break;
        }
        // make sure that the new logl is not so bad compared with previous logl
        ASSERT(cur_lh > tree_lh - 1.0 && "branch length opt reduces LnL");
        tree_lh = cur_lh;
    }
    logResultOfParameterOptimization(write_info, begin_time,
                                     i, report_to_tree);
    return tree_lh;
}

double PartitionModelPlen::optimizeSubtreeModelParameters
            (PhyloSuperTreePlen *tree,
             int iteration,
             double gradient_epsilon,
             PhyloTree* report_to_tree) {
    double cur_lh = 0;
    int    ntrees = static_cast<int>(tree->size());
    #ifdef _OPENMP
    #pragma omp parallel for reduction(+: cur_lh) schedule(dynamic) if(tree->num_threads > 1)
    #endif
    for (int partid = 0; partid < ntrees; partid++) {
        int  part    = tree->part_order[partid];
        auto subtree = tree->at(part);
        auto factory = subtree->getModelFactory();
        auto divisor = min(min(iteration,ntrees),10);
        tree->part_info[part].cur_score = factory->
            optimizeParametersOnly(iteration+1, 
                                   gradient_epsilon/divisor,
                                   tree->part_info[part].cur_score,
                                   report_to_tree);

        if (tree->part_info[part].cur_score == 0.0) {
            tree->part_info[part].cur_score = subtree->computeLikelihood();
        }
        cur_lh += tree->part_info[part].cur_score;
        
        
        // normalize rates s.t. branch lengths are #subst per site
        double mean_rate = subtree->getRate()->rescaleRates();
        if (fabs(mean_rate-1.0) > 1e-6) {
            if (tree->fixed_rates) {
                outError("Unsupported -spj."
                         " Please use proportion edge-linked"
                         " partition model (-spp)");
            }
            subtree->scaleLength(mean_rate);
            tree->part_info[part].part_rate *= mean_rate;
        }   
    }
    return cur_lh;
}

double PartitionModelPlen::optimizeGeneRate
            (PhyloSuperTreePlen *tree,  double tree_lh,
             double cur_lh, double gradient_epsilon) {
    // Optimizing gene rate
    if(tree->fixed_rates){
        return cur_lh;
    }
    cur_lh = optimizeGeneRate(gradient_epsilon);
    if (verbose_mode >= VerboseMode::VB_MED) {
        cout << "LnL after optimizing partition-specific rates: " 
             << cur_lh << endl;
        writeInfo(cout);
    }
    ASSERT(cur_lh > tree_lh - 1.0 && "partition rate opt reduces LnL");
    return cur_lh;
}

double PartitionModelPlen::optimizeBranchLengthsIfRequested
            (PhyloSuperTreePlen *tree, 
             int fixed_len, int iteration,
             double cur_lh, double logl_epsilon,
             double gradient_epsilon) {
    // Optimizing branch lengths
    int my_iter = min(5,iteration+1);
    
    if (fixed_len == BRLEN_OPTIMIZE){
        double new_lh = tree->optimizeAllBranches(my_iter, logl_epsilon);
        ASSERT(new_lh > cur_lh - 1.0);
        cur_lh = new_lh;
    } else if (fixed_len == BRLEN_SCALE) {
        double scaling = 1.0;
        double new_lh = tree->optimizeTreeLengthScaling(MIN_BRLEN_SCALE, scaling,
                                                        MAX_BRLEN_SCALE, gradient_epsilon);
        ASSERT(new_lh > cur_lh - 1.0);
        cur_lh = new_lh;
    }
    return cur_lh;
}

void PartitionModelPlen::logResultOfParameterOptimization
        (bool write_info, double begin_time, 
         int iteration, PhyloTree* report_to_tree) {
    //    cout <<"OPTIMIZE MODEL has finished"<< endl;
    if (write_info) {
        writeInfo(cout);
    }
    // write linked_models
    if (verbose_mode <= VerboseMode::VB_MIN && write_info) {
        for (auto it = linked_models.begin(); it != linked_models.end(); it++) {
            it->second->writeInfo(cout);
        }
    }
    report_to_tree->hideProgress();
    cout << "Parameters optimization took " << iteration-1 << " rounds"
         << " (" << getRealTime()-begin_time << " sec)" << endl << endl;
    report_to_tree->showProgress();
}

double PartitionModelPlen::optimizeParametersGammaInvar(int fixed_len, bool write_info,
                                                        double logl_epsilon, double gradient_epsilon,
                                                        PhyloTree* report_to_tree) {
    outError("Thorough I+G parameter optimization does not work"
             " with edge-linked partition model yet");
    return 0.0;
}

void PartitionModelPlen::writeInfo(ostream &out) {
    auto tree = dynamic_cast<PhyloSuperTreePlen*>(site_rate->getTree());
    ASSERT(tree!=nullptr);
    auto ntrees = tree->size();
    if (!tree->fixed_rates) {
        out << "Partition-specific rates: ";
        for(size_t part = 0; part < ntrees; part++){
            out << " " << tree->part_info[part].part_rate;
        }
        out << endl;
    }
}

double PartitionModelPlen::optimizeGeneRate(double gradient_epsilon)
{
    auto tree = dynamic_cast<PhyloSuperTreePlen*>(site_rate->getTree());
    ASSERT(tree!=nullptr);
    // BQM 22-05-2015: change to optimize individual rates
    double score = 0.0;
    size_t nsites = tree->getAlnNSite();
    
    vector<DoubleVector> brlen;
    brlen.resize(tree->branchNum);
    tree->getBranchLengths(brlen);
    double max_brlen = 0.0;
    for (size_t i = 0; i < brlen.size(); ++i) {
        for (size_t j = 0; j < brlen[i].size(); ++j) {
            if (brlen[i][j] > max_brlen) {
                max_brlen = brlen[i][j];
            }
        }
    }
    if (tree->part_order.empty()) tree->computePartitionOrder();
    
#ifdef _OPENMP
#pragma omp parallel for reduction(+: score) schedule(dynamic) if(tree->num_threads > 1)
#endif
    for (int j = 0; j < tree->size(); j++) {
        int i = tree->part_order[j];
        double min_scaling = 1.0/tree->at(i)->getAlnNSite();
        double max_scaling = static_cast<double>(nsites / (double)tree->at(i)->getAlnNSite());
        if (max_scaling < tree->part_info[i].part_rate)
            max_scaling = tree->part_info[i].part_rate;
        if (min_scaling > tree->part_info[i].part_rate)
            min_scaling = tree->part_info[i].part_rate;
        auto t_score = tree->at(i)->optimizeTreeLengthScaling(min_scaling, tree->part_info[i].part_rate, 
                                                              max_scaling, gradient_epsilon);
        tree->part_info[i].cur_score = t_score;
        score += tree->part_info[i].cur_score;
    }
    // now normalize the rates
    double sum = 0.0;
    size_t nsite = 0;
    for (size_t i = 0; i < tree->size(); ++i) {
        sum += tree->part_info[i].part_rate * tree->at(i)->aln->getNSite();
        auto seq = tree->at(i)->aln->seq_type;
        if (seq == SeqType::SEQ_CODON && tree->rescale_codon_brlen)
            nsite += 3*tree->at(i)->aln->getNSite();
        else
            nsite += tree->at(i)->aln->getNSite();
    }
    sum /= nsite;
    
    if (sum > tree->params->max_branch_length / max_brlen) {
        outWarning("Too high (saturated) partition rates"
                   " for proportional partition model!");
//        outWarning("Please switch to the edge-equal"
//                   " partition model via -q option instead of -spp");
//        exit(EXIT_FAILURE);
    }
    tree->scaleLength(sum);
    sum = 1.0/sum;
    for (size_t i = 0; i < tree->size(); ++i)
        tree->part_info[i].part_rate *= sum;
    return score;
}


int PartitionModelPlen::getNParameters(int brlen_type) const {
    auto tree = dynamic_cast<PhyloSuperTreePlen*>(site_rate->getTree());
    ASSERT(tree!=nullptr);
    int df = 0;
    for (auto it = tree->begin(); it != tree->end(); it++) {
        df += (*it)->getModelFactory()->model->getNDim() +
        (*it)->getModelFactory()->model->getNDimFreq() +
        (*it)->getModelFactory()->site_rate->getNDim();
    }
    df += tree->branchNum;
    if (!tree->fixed_rates) {
        df += static_cast<int>(tree->size()) - 1;
    }
    if (linked_alpha > 0.0)
        df ++;
    for (auto it = linked_models.begin(); it != linked_models.end(); it++) {
        bool fixed = it->second->fixParameters(false);
        df += it->second->getNDim() + it->second->getNDimFreq();
        it->second->fixParameters(fixed);
    }
    return df;
}

/*
int PartitionModelPlen::getNDim() const{
    auto tree = dynamic_cast<PhyloSuperTreePlen*>(site_rate->getTree());
    ASSERT(tree!=nullptr);
    int ndim = tree->size() -1;
    return ndim;
}
*/
