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

/*! \file TimeVectorsChebyshev.h
 *
 *
 */

#ifndef TIME_VECTORS_CHEBYSHEV
#define TIME_VECTORS_CHEBYSHEV
#include <iostream>
#include <vector>
#include "TimeVectorsBase.h"
#include "ParallelTriDiag.h"
#include "NoPthreadsNg.h"
#include "Parallelizer.h"
#include "ScaledHamiltonian.h"

namespace Dmrg {

template<typename TargetParamsType,
         typename ModelType,
         typename WaveFunctionTransfType,
         typename LanczosSolverType,
         typename VectorWithOffsetType>
class TimeVectorsChebyshev : public  TimeVectorsBase<TargetParamsType,
        ModelType,
        WaveFunctionTransfType,
        LanczosSolverType,
        VectorWithOffsetType> {

	typedef TimeVectorsBase<TargetParamsType,
	ModelType,
	WaveFunctionTransfType,
	LanczosSolverType,
	VectorWithOffsetType> BaseType;
	typedef typename BaseType::PairType PairType;
	typedef typename TargetParamsType::RealType RealType;
	typedef typename PsimagLite::Vector<RealType>::Type VectorRealType;
	typedef typename ModelType::ModelHelperType ModelHelperType;
	typedef typename ModelHelperType::LeftRightSuperType LeftRightSuperType;
	typedef typename LeftRightSuperType::BasisWithOperatorsType
	BasisWithOperatorsType;
	typedef typename BasisWithOperatorsType::SparseMatrixType SparseMatrixType;
	typedef typename SparseMatrixType::value_type ComplexOrRealType;
	typedef typename PsimagLite::Matrix<ComplexOrRealType> MatrixRealType;
	typedef ParallelTriDiag<ModelType,LanczosSolverType,VectorWithOffsetType>
	ParallelTriDiagType;
	typedef typename ParallelTriDiagType::MatrixComplexOrRealType
	MatrixComplexOrRealType;
	typedef typename ParallelTriDiagType::TargetVectorType TargetVectorType;
	typedef typename ParallelTriDiagType::VectorMatrixFieldType
	VectorMatrixFieldType;
	typedef typename LanczosSolverType::TridiagonalMatrixType TridiagonalMatrixType;
	typedef typename ModelType::InputValidatorType InputValidatorType;
	typedef typename PsimagLite::Vector<VectorWithOffsetType>::Type
	VectorVectorWithOffsetType;
	typedef typename PsimagLite::Vector<VectorRealType>::Type VectorVectorRealType;
	typedef typename LanczosSolverType::MatrixType MatrixLanczosType;
	typedef ScaledHamiltonian<MatrixLanczosType, TargetParamsType> ScaledMatrixType;
	typedef PsimagLite::Vector<SizeType>::Type VectorSizeType;

public:

	TimeVectorsChebyshev(const RealType& currentTime,
	                     const VectorRealType& times,
	                     VectorVectorWithOffsetType& targetVectors,
	                     const ModelType& model,
	                     const WaveFunctionTransfType& wft,
	                     const LeftRightSuperType& lrs,
	                     const RealType& E0,
	                     InputValidatorType& ioIn)
	    : currentTime_(currentTime),
	      times_(times),
	      targetVectors_(targetVectors),
	      model_(model),
	      wft_(wft),
	      lrs_(lrs),
	      E0_(E0),
	      ioIn_(ioIn),
	      timeHasAdvanced_(false)
	{}

	virtual void calcTimeVectors(const PairType&,
	                             RealType,
	                             const VectorWithOffsetType& phi,
	                             const ProgramGlobals::DirectionEnum,
	                             bool,
	                             const VectorSizeType& indices,
	                             const TargetParamsType& tstStruct)
	{
		SizeType n = indices.size();
		assert(n > 0);
		if (currentTime_==0 && tstStruct.noOperator() && tstStruct.skipTimeZero()) {
			for (SizeType i = 0; i < n; ++i) {
				SizeType ii = indices[i];
				targetVectors_[ii] = phi;
			}

			return;
		}

		const VectorWithOffsetType* ptr0 = &(targetVectors_[indices[0]]);
		const VectorWithOffsetType* ptr1 = &phi;
		if (ptr0 != ptr1)
			targetVectors_[indices[0]] = phi;

		if (times_.size() == 1 && fabs(times_[0])<1e-10) return;

		if (timeHasAdvanced_) {
			for (SizeType i = 0; i < n - 1; ++i) {
				SizeType ii = indices[i];
				targetVectors_[ii] = targetVectors_[ii + 1];
			}
		}

		for (SizeType i = 2; i < n; ++i) {
			SizeType ii = indices[i];
			assert(ii < targetVectors_.size());
			assert(ii != 1);
			targetVectors_[ii] = phi;
			SizeType prev = indices[i - 1];
			SizeType prevMinus2 = indices[i - 2];
			calcTargetVector(targetVectors_[ii], phi, prev, prevMinus2, tstStruct);
		}

		timeHasAdvanced_ = false;
	}

	void timeHasAdvanced()
	{
		timeHasAdvanced_ = true;
	}


private:

	void calcTargetVector(VectorWithOffsetType& v,
	                      const VectorWithOffsetType& phi,
	                      SizeType prev,
	                      SizeType prevMinus2,
	                      const TargetParamsType& tstStruct)

	{
		for (SizeType ii=0;ii<phi.sectors();ii++) {
			SizeType i0 = phi.sector(ii);
			TargetVectorType r;
			calcTargetVector(r, phi, prev, prevMinus2, i0, tstStruct);
			v.setDataInSector(r,i0);
		}
	}

	void calcTargetVector(TargetVectorType& r,
	                      const VectorWithOffsetType& phi,
	                      SizeType prev,
	                      SizeType prevMinus2,
	                      SizeType i0,
	                      const TargetParamsType& tstStruct)
	{
		SizeType p = lrs_.super().findPartitionNumber(phi.offset(i0));
		typename ModelType::HamiltonianConnectionType hc(p,
		                                                 lrs_,
		                                                 model_.geometry(),
		                                                 ModelType::modelLinks(),
		                                                 currentTime_,
		                                                 0);
		MatrixLanczosType lanczosHelper(model_,
		                                hc);

		ProgramGlobals::VerboseEnum verbose = (model_.params().options.find("VerboseCheby")
		                                       != PsimagLite::String::npos)
		        ? ProgramGlobals::VerboseEnum::YES : ProgramGlobals::VerboseEnum::NO;

		// defining Hprime matrix:
		ScaledMatrixType lanczosHelper2(lanczosHelper, tstStruct, E0_, verbose);

		SizeType total = phi.effectiveSize(i0);
		TargetVectorType phi2(total);
		r.resize(total);
		if (currentTime_ == 0) {
			phi.extract(phi2,i0);
			lanczosHelper2.matrixVectorProduct(r,phi2); // applying Hprime
		} else {
			TargetVectorType x2(total);
			VectorWithOffsetType x = 2.0*targetVectors_[prev];
			x.extract(x2,i0);
			targetVectors_[prevMinus2].extract(phi2,i0);
			lanczosHelper2.matrixVectorProduct(r,x2); // applying Hprime
			r += (-1.0)*phi2;
		}
	}

	const RealType& currentTime_;
	const VectorRealType& times_;
	VectorVectorWithOffsetType& targetVectors_;
	const ModelType& model_;
	const WaveFunctionTransfType& wft_;
	const LeftRightSuperType& lrs_;
	const RealType& E0_;
	InputValidatorType& ioIn_;
	bool timeHasAdvanced_;
}; //class TimeVectorsChebyshev
} // namespace Dmrg
/*@}*/
#endif

