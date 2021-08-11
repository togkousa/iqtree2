//
//  Copyright (C) 2020, James Barbetti.
//
//  LICENSE:
//* This program is free software; you can redistribute it and/or modify
//* it under the terms of the GNU General Public License as published by
//* the Free Software Foundation; either version 2 of the License, or
//* (at your option) any later version.
//*
//* This program is distributed in the hope that it will be useful,
//* but WITHOUT ANY WARRANTY; without even the implied warranty of
//* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//* GNU General Public License for more details.
//*
//* You should have received a copy of the GNU General Public License
//* along with this program; if not, write to the
//* Free Software Foundation, Inc.,
//* 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//

#ifndef starttree_h
#define starttree_h

#include <string>
#include <map>
#include <iostream>
#include <vector>
#include "timeutil.h"       //for getRealTime()
#include "vectortypes.h"    //for StrVector template class

namespace StartTree
{
    class BuilderInterface
    {
    public:
        BuilderInterface() {}
        virtual ~BuilderInterface() {}
        virtual bool isBenchmark() const = 0;
        virtual void setZippedOutput(bool zipIt) = 0;
        virtual bool constructTree
            ( const std::string &distanceMatrixFilePath
             , const std::string & newickTreeFilePath) = 0;
        virtual bool constructTreeInMemory
            ( const StrVector &sequenceNames
             , const double *distanceMatrix
             , const std::string & newickTreeFilePath) = 0;
        virtual const std::string& getName() const = 0;
        virtual const std::string& getDescription() = 0;
        virtual void beSilent() = 0;
        virtual void setPrecision(int precision) = 0;
    };

    class BenchmarkingTreeBuilder;

    class Factory
    {
        friend class BenchmarkingTreeBuilder;
    private:
        std::map<std::string, BuilderInterface*> mapOfTreeBuilders;
        //Note: Owned by the Factory, and will be deleted in ~Factory.
        std::string nameOfDefaultTreeBuilder;
        
    protected:
        Factory();
        ~Factory();

        size_t getBuilderCount();
        void addBuilder(const std::string& name, BuilderInterface* builder);
        BuilderInterface* getBuilder(const std::string& name);
        BuilderInterface* getBuilder(const char* name);
    public:
        static Factory& getInstance();
        void   advertiseTreeBuilder(BuilderInterface* builder);
        void   setNameOfDefaultTreeBuilder(const char* name);
        static const std::string& getNameOfDefaultTreeBuilder();
        static BuilderInterface* getTreeBuilderByName(const std::string& name);
        std::string getListOfTreeBuilders() const;
    };

    template <class B> class Builder: public BuilderInterface
    {
        //Note: B must have:
        //      1. a constructor that takes the name of an ".mldist"
        //         distance matrix file as a parameter;
        //      2. a constructTree() member function; and
        //      3. a writeTreeFile() member function.
        //
    protected:
        const std::string name;
        const std::string description;
        bool  silent;
        bool  isOutputToBeZipped;
        int   precision;
        bool  constructTreeWith(B& builder) {
            double buildStart    = getRealTime();
            double buildStartCPU = getCPUTime();
            if (silent) {
                builder.beSilent();
            }
            builder.constructTree();
            double buildElapsed = getRealTime() - buildStart;
            double buildCPU     = getCPUTime() - buildStartCPU;
            if (!silent) {
                std::cout.precision(6);
                std::cout << "Computing "
                          << name << " tree took " 
                          << buildElapsed << " sec (of wall-clock time) " 
                          << buildCPU << " sec (of CPU time)" ;
                if (0<buildElapsed) {
                    double percentCPU = (buildCPU/buildElapsed)*100.0;
                    std::cout << "(" << floor(percentCPU) << "%)";
                }
                std::cout << std::endl;
                std::cout.precision(3);
            }
            return true;
        }
    public:
        Builder(const char* nameToUse, const char *descriptionToGive)
        : name(nameToUse), description(descriptionToGive), silent(false)
        , isOutputToBeZipped(false), precision(6) {
        }
        virtual void beSilent() override {
            silent = true;
        }
        virtual const std::string& getName() const override{
            return name;
        }
        virtual const std::string& getDescription() override {
            return description;
        }
        virtual bool isBenchmark() const override {
            return false;
        }
        virtual void setZippedOutput(bool zipIt) override {
            isOutputToBeZipped = zipIt;
        }
        virtual bool constructTree
            ( const std::string &distanceMatrixFilePath
             , const std::string & newickTreeFilePath) override {
            B builder;
            if (silent) {
                builder.beSilent();
            }
            if (!builder.loadMatrixFromFile(distanceMatrixFilePath)) {
                return false;
            }
            constructTreeWith(builder);
            if (newickTreeFilePath.empty()) {
                return true;
            }
            builder.setZippedOutput(isOutputToBeZipped);
            return builder.writeTreeFile(precision, newickTreeFilePath);
        }
        virtual bool constructTreeInMemory
            ( const StrVector &sequenceNames
            , const double *distanceMatrix
            , const std::string & newickTreeFilePath) override {
            B builder;
            if (silent) {
                builder.beSilent();
            }
            if (!builder.loadMatrix(sequenceNames, distanceMatrix)) {
                return false;
            }
            constructTreeWith(builder);
            builder.setZippedOutput(isOutputToBeZipped);
            if (newickTreeFilePath.empty()) {
                return true;
            }
            return builder.writeTreeFile(precision, newickTreeFilePath);
        }
        virtual void setPrecision(int precision_to_use) override {
            precision = precision_to_use;
        }
    };

    class BenchmarkingTreeBuilder: public BuilderInterface
    {
    protected:
        const std::string name;
        const std::string description;
        std::vector<BuilderInterface*> builders;
        bool isOutputToBeZipped;
        bool silent;
        int  precision;
    public:
        BenchmarkingTreeBuilder(Factory& f, const char* nameToUse, 
                                const char *descriptionToGive);
        virtual const std::string& getName() const override;
        virtual const std::string& getDescription() override;
        virtual bool isBenchmark() const override;
        virtual bool constructTree
            ( const std::string& distanceMatrixFilePath
            , const std::string& newickTreeFilePath) override;
        virtual bool constructTreeInMemory
            ( const StrVector &sequenceNames
            , const double *distanceMatrix
            , const std::string& newickTreeFilePath) override;
        virtual void setZippedOutput(bool zipIt) override;
        virtual void beSilent() override;
        virtual void setPrecision(int precisionToUse) override;
    };
}

#define START_TREE_RECOGNIZED(name) \
    ( StartTree::Factory::getTreeBuilderByName(name) != nullptr )

#endif /* starttree_h */
