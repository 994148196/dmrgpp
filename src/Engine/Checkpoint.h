/*
Copyright (c) 2009-2012, UT-Battelle, LLC
All rights reserved

[DMRG++, Version 2.0.0]
[by G.A., Oak Ridge National Laboratory]

UT Battelle Open Source Software License 11242008

OPEN SOURCE LICENSE

Subject to the conditions of this License, each
contributor to this software hereby grants, free of
charge, to any person obtaining a copy of this software
and associated documentation files (the "Software"), a
perpetual, worldwide, non-exclusive, no-charge,
royalty-free, irrevocable copyright license to use, copy,
modify, merge, publish, distribute, and/or sublicense
copies of the Software.

1. Redistributions of Software must retain the above
copyright and license notices, this list of conditions,
and the following disclaimer.  Changes or modifications
to, or derivative works of, the Software should be noted
with comments and the contributor and organization's
name.

2. Neither the names of UT-Battelle, LLC or the
Department of Energy nor the names of the Software
contributors may be used to endorse or promote products
derived from this software without specific prior written
permission of UT-Battelle.

3. The software and the end-user documentation included
with the redistribution, with or without modification,
must include the following acknowledgment:

"This product includes software produced by UT-Battelle,
LLC under Contract No. DE-AC05-00OR22725  with the
Department of Energy."

*********************************************************
DISCLAIMER

THE SOFTWARE IS SUPPLIED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT OWNER, CONTRIBUTORS, UNITED STATES GOVERNMENT,
OR THE UNITED STATES DEPARTMENT OF ENERGY BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

NEITHER THE UNITED STATES GOVERNMENT, NOR THE UNITED
STATES DEPARTMENT OF ENERGY, NOR THE COPYRIGHT OWNER, NOR
ANY OF THEIR EMPLOYEES, REPRESENTS THAT THE USE OF ANY
INFORMATION, DATA, APPARATUS, PRODUCT, OR PROCESS
DISCLOSED WOULD NOT INFRINGE PRIVATELY OWNED RIGHTS.

*********************************************************


*/
/** \ingroup DMRG */
/*@{*/

/*! \file Checkpoint.h
 *
 *  checkpointing functions
 *  this class also owns the stacks since they
 *  are so related to checkpointing
 */
#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include "Stack.h"
#include "DiskStackNg.h"
#include "ProgressIndicator.h"
#include "ProgramGlobals.h"
#include "Io/IoSelector.h"
#include "DiskOrMemoryStack.h"

namespace Dmrg {

template<typename ModelType, typename WaveFunctionTransfType_>
class Checkpoint {

public:

	typedef WaveFunctionTransfType_ WaveFunctionTransfType;
	typedef typename ModelType::RealType  RealType;
	typedef typename ModelType::ParametersType ParametersType;
	typedef typename ModelType::BasisWithOperatorsType BasisWithOperatorsType;
	typedef typename BasisWithOperatorsType::ComplexOrRealType ComplexOrRealType;
	typedef typename BasisWithOperatorsType::OperatorsType OperatorsType;
	typedef typename PsimagLite::IoSelector IoType;
	typedef typename ModelType::InputValidatorType InputValidatorType;
	typedef typename OperatorsType::OperatorType OperatorType;
	typedef typename OperatorType::StorageType SparseMatrixType;
	typedef DiskOrMemoryStack<BasisWithOperatorsType> DiskOrMemoryStackType;
	typedef typename BasisWithOperatorsType::QnType QnType;
	typedef typename QnType::VectorQnType VectorQnType;
	typedef DiskStack<BasisWithOperatorsType>  DiskStackType;
	typedef PsimagLite::Vector<PsimagLite::String>::Type VectorStringType;
	typedef PsimagLite::Vector<SizeType>::Type VectorSizeType;

	Checkpoint(const ParametersType& parameters,
	           InputValidatorType& ioIn,
	           const ModelType& model,
	           bool isObserveCode) :
	    parameters_(parameters),
	    isObserveCode_(isObserveCode),
	    isRestart_(parameters_.options.find("restart") != PsimagLite::String::npos),
	    systemStack_(parameters_.options.find("shrinkStacksOnDisk") != PsimagLite::String::npos,
	                 parameters_.filename,
	                 "system",
	                 isObserveCode),
	    envStack_(systemStack_.onDisk(),
	              parameters_.filename,
	              "environ",
	              isObserveCode),
	    progress_("Checkpoint"),
	    energyFromFile_(0.0),
	    dummyBwo_("dummy")
	{
		if (parameters_.autoRestart) isRestart_ = true;

		SizeType site = 0; // FIXME for Immm model, find max of hilbert(site) over site
		SizeType hilbertOneSite = model.hilbertSize(site);
		if (parameters_.keptStatesInfinite > 0 &&
		        parameters_.keptStatesInfinite < hilbertOneSite) {
			PsimagLite::String str("FATAL:  keptStatesInfinite= ");
			str += ttos(parameters_.keptStatesInfinite) + " < ";
			str += ttos(hilbertOneSite) + "\n";
			throw PsimagLite::RuntimeError(str);
		}

		checkFiniteLoops(model.geometry().numberOfSites(), hilbertOneSite, ioIn);

		if (!isRestart_) return;

		VectorSizeType v;

		{
			IoType::In ioIn2(parameters_.checkpoint.filename);
			ioIn2.readLastVectorEntry(energyFromFile_,
			                          parameters_.checkpoint.labelForEnergy);

			ioIn2.read(v, "CHKPOINTSYSTEM/OperatorPerSite");
			if (v.size() == 0) return;

			bool iscomplex = false;
			ioIn2.read(iscomplex, "IsComplex");
			ioIn2.close();
			if (iscomplex != PsimagLite::IsComplexNumber<ComplexOrRealType>::True)
				err("Previous run was complex and this one is not (or viceversa)\n");
		}

		SizeType operatorsPerSite = v[0];

		typename PsimagLite::Vector<OperatorType>::Type creationMatrix;
		VectorSizeType test(1,0);
		VectorQnType qq;
		model.setOperatorMatrices(creationMatrix, qq, test);

		if (creationMatrix.size() != operatorsPerSite) {
			PsimagLite::String msg("CheckPoint: FATAL: Perhaps trying to");
			msg += " restart one model from a different one or different variant\n";
			throw PsimagLite::RuntimeError(msg);
		}

		loadStacksDiskToMemory();
	}

	~Checkpoint()
	{
		if (parameters_.options.find("noSaveStacks") != PsimagLite::String::npos)
			return;

		loadStacksMemoryToDisk();
	}

	void checkpointStacks(PsimagLite::String filename) const
	{
		// taken from dtor
		sayAboutToWrite();
		const bool needsToRead = false;

		DiskStackType systemDisk(filename, needsToRead, "system", isObserveCode_);
		systemStack_.toDisk(systemDisk);

		DiskStackType environDisk(filename, needsToRead, "environ", isObserveCode_);
		envStack_.toDisk(environDisk);
		sayWritingDone();
	}

	// Not related to stacks
	void write(const BasisWithOperatorsType &pS,
	           const BasisWithOperatorsType &pE,
	           typename IoType::Out& io) const
	{
		PsimagLite::OstringStream msg;
		msg<<"Saving pS and pE...";
		progress_.printline(msg,std::cout);
		pS.write(io,
		         "CHKPOINTSYSTEM",
		         IoType::Out::Serializer::NO_OVERWRITE,
		         BasisWithOperatorsType::SaveEnum::ALL);
		pE.write(io,
		         "CHKPOINTENVIRON",
		         IoType::Out::Serializer::NO_OVERWRITE,
		         BasisWithOperatorsType::SaveEnum::ALL);
	}

	// Not related to stacks
	void read(BasisWithOperatorsType &pS,
	          BasisWithOperatorsType &pE,
	          bool isObserveCode)
	{
		typename PsimagLite::IoSelector::In ioTmp(parameters_.checkpoint.filename);

		BasisWithOperatorsType pS1(ioTmp, "CHKPOINTSYSTEM", isObserveCode);
		pS = pS1;

		BasisWithOperatorsType pE1(ioTmp, "CHKPOINTENVIRON", isObserveCode);
		pE = pE1;
	}

	void push(const BasisWithOperatorsType &pS,const BasisWithOperatorsType &pE)
	{
		systemStack_.push(pS);
		envStack_.push(pE);
	}

	void push(const BasisWithOperatorsType &pSorE,
	          typename ProgramGlobals::SysOrEnvEnum what)
	{
		if (what == ProgramGlobals::SysOrEnvEnum::ENVIRON) envStack_.push(pSorE);
		else systemStack_.push(pSorE);
	}

	const BasisWithOperatorsType& shrink(typename ProgramGlobals::SysOrEnvEnum what)
	{
		return (what == ProgramGlobals::SysOrEnvEnum::ENVIRON) ? shrinkInternal(envStack_) :
		                                                         shrinkInternal(systemStack_);
	}

	bool isRestart() const { return isRestart_; }

	SizeType stackSize(typename ProgramGlobals::SysOrEnvEnum what) const
	{
		return (what == ProgramGlobals::SysOrEnvEnum::ENVIRON) ? envStack_.size() :
		                                                         systemStack_.size();
	}

	const DiskOrMemoryStackType& memoryStack(typename ProgramGlobals::SysOrEnvEnum option) const
	{
		return (option == ProgramGlobals::SysOrEnvEnum::SYSTEM) ? systemStack_ :
		                                                          envStack_;
	}

	const ParametersType& parameters() const { return parameters_; }

	const RealType& energy() const { return energyFromFile_; }

private:

	void sayAboutToWrite() const
	{
		PsimagLite::OstringStream msg;
		msg<<"Writing sys. and env. stacks to disk...";
		progress_.printline(msg, std::cout);
	}

	void sayWritingDone() const
	{
		PsimagLite::OstringStream msg;
		msg<<"Written sys. and env. stacks to disk.";
		progress_.printline(msg, std::cout);
	}

	Checkpoint(const Checkpoint&);

	Checkpoint& operator=(const Checkpoint&);

	void checkFiniteLoops(SizeType totalSites,
	                      SizeType hilbertOneSite,
	                      InputValidatorType& ioIn) const
	{
		if (parameters_.options.find("nofiniteloops")!=PsimagLite::String::npos)
			return;

		bool allInSystem = (parameters_.options.find("geometryallinsystem")!=
		        PsimagLite::String::npos);

		PsimagLite::Vector<FiniteLoop>::Type vfl;
		int lastSite = (allInSystem) ? totalSites-2 : totalSites/2-1; // must be signed
		int prevDeltaSign = 1;
		bool checkPoint = false;

		if (isRestart_) {
			PsimagLite::IoSelector::In io1(parameters_.checkpoint.filename);
			io1.read(lastSite, "FinalPsi/TargetCentralSite");
			io1.read(prevDeltaSign, "LastLoopSign");
			checkPoint = true;
		}

		if (totalSites & 1) lastSite++;

		ParametersType::readFiniteLoops(ioIn,vfl);

		if (!parameters_.autoRestart)
			checkFiniteLoops(vfl,totalSites,lastSite,prevDeltaSign,checkPoint);

		checkMvalues(vfl, hilbertOneSite);
	}

	void checkFiniteLoops(const PsimagLite::Vector<FiniteLoop>::Type& finiteLoop,
	                      SizeType totalSites,
	                      SizeType lastSite,
	                      int prevDeltaSign,
	                      bool checkPoint) const
	{
		PsimagLite::String s = "checkFiniteLoops: I'm falling out of the lattice ";
		PsimagLite::String loops = "";
		int x = lastSite;

		if (finiteLoop[0].stepLength<0 && !checkPoint) x++;

		SizeType sopt = 0; // have we started saving yet?
		for (SizeType i=0;i<finiteLoop.size();i++)  {
			SizeType thisSaveOption = (finiteLoop[i].saveOption & 1);
			if (sopt == 1 && !(thisSaveOption&1)) {
				s = "Error for finite loop number " + ttos(i) + "\n";
				s += "Once you say 1 on a finite loop, then all";
				s += " finite loops that follow must have 1.";
				throw PsimagLite::RuntimeError(s.c_str());
			}

			// naive location:
			int delta = finiteLoop[i].stepLength;
			x += delta;
			loops = loops + ttos(delta) + " ";

			// take care of bounces:
			bool b1 = (checkPoint || (i>0));
			if (b1 && delta*prevDeltaSign < 0) x += prevDeltaSign;
			prevDeltaSign = 1;
			if (delta<0) prevDeltaSign = -1;

			// check that we don't fall out
			bool flag = false;
			if (x<=0) {
				s = s + "on the left end\n";
				flag = true;
			}
			if (SizeType(x)>=totalSites-1) {
				s = s + "on the right end\n";
				flag = true;
			}
			if (flag) {
				// complain and die if we fell out:
				s = s + "Loops so far: " + loops + "\n";
				s =s + "x=" + ttos(x) + " last delta=" +
				        ttos(delta);
				s =s + " sites=" + ttos(totalSites);
				throw PsimagLite::RuntimeError(s.c_str());
			}
		}
	}

	void checkMvalues(const PsimagLite::Vector<FiniteLoop>::Type& finiteLoop,
	                  SizeType hilbertOneSite) const
	{
		for (SizeType i = 0;i < finiteLoop.size(); ++i)  {
			if (finiteLoop[i].keptStates >= hilbertOneSite)
				continue;
			PsimagLite::String str("FATAL: Finite loop number ");
			str += ttos(i) +" has keptStates= " + ttos(finiteLoop[i].keptStates);
			str += " < " + ttos(hilbertOneSite) + "\n";
			throw PsimagLite::RuntimeError(str);
		}
	}

	//! shrink  (we don't really shrink, we just undo the growth)
	const BasisWithOperatorsType& shrinkInternal(DiskOrMemoryStackType& thisStack)
	{
		assert(thisStack.size() > 0);
		thisStack.pop();
		assert(thisStack.size() > 0);
		dummyBwo_ =  thisStack.top();
		return dummyBwo_;
	}

	void loadStacksDiskToMemory()
	{
		DiskStackType systemDisk(parameters_.checkpoint.filename,
		                         isRestart_,
		                         "system",
		                         isObserveCode_);
		DiskStackType envDisk(parameters_.checkpoint.filename,
		                      isRestart_,
		                      "environ",
		                      isObserveCode_);
		PsimagLite::OstringStream msg;
		msg<<"Loading sys. and env. stacks from disk...";
		progress_.printline(msg,std::cout);

		DiskOrMemoryStackType::loadStack(systemStack_, systemDisk);
		DiskOrMemoryStackType::loadStack(envStack_, envDisk);
	}

	void loadStacksMemoryToDisk()
	{
		const bool needsToRead = false;
		DiskStackType systemDisk(parameters_.filename,
		                         needsToRead,
		                         "system",
		                         isObserveCode_);
		DiskStackType envDisk(parameters_.filename,
		                      needsToRead,
		                      "environ",
		                      isObserveCode_);
		sayAboutToWrite();
		DiskOrMemoryStackType::loadStack(systemDisk, systemStack_);
		DiskOrMemoryStackType::loadStack(envDisk, envStack_);
		sayWritingDone();
	}

	//! Move elsewhere
	//! returns s1+s2 if s2 has no '/',
	//! if s2 = s2a + '/' + s2b return s2a + '/' + s1 + s2b
	PsimagLite::String appendWithDir(const PsimagLite::String& s1,
	                                 const PsimagLite::String& s2) const
	{
		size_t x = s2.find("/");
		if (x==PsimagLite::String::npos) return s1 + s2;
		PsimagLite::String suf = s2.substr(x+1,s2.length());
		PsimagLite::String dir = s2.substr(0,s2.length()-suf.length());
		return dir + s1 + suf;
	}

	const ParametersType& parameters_;
	bool isObserveCode_;
	bool isRestart_;
	DiskOrMemoryStackType systemStack_;
	DiskOrMemoryStackType envStack_;
	PsimagLite::ProgressIndicator progress_;
	RealType energyFromFile_;
	BasisWithOperatorsType dummyBwo_;
}; // class Checkpoint
} // namespace Dmrg

/*@}*/
#endif

