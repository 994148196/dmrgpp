/*
Copyright (c) 2009-2013, UT-Battelle, LLC
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
// END LICENSE BLOCK
/** \ingroup DMRG */
/*@{*/

/*! \file BlockMatrix.h
 *
 *  A class to represent a block diagonal matrix
 *
 */
#ifndef BLOCKMATRIX_HEADER_H
#define BLOCKMATRIX_HEADER_H
#include <vector>
#include <iostream>
#include "Matrix.h" // in PsimagLite
#include "ProgramGlobals.h"
#include "Concurrency.h"
#include "Parallelizer.h"

namespace Dmrg {

	//! A block matrix class
	//! Blocks can be of any type and are templated with the type MatrixInBlockTemplate
	//
	template<class T,class MatrixInBlockTemplate>
	class BlockMatrix {

	public:

		typedef MatrixInBlockTemplate BuildingBlockType;

		class LoopForDiag {

			typedef PsimagLite::Concurrency ConcurrencyType;
			typedef BlockMatrix<T,MatrixInBlockTemplate> BlockMatrixType;
			typedef typename PsimagLite::Real<T>::Type RealType;
		public:

			LoopForDiag(BlockMatrixType  &C1,
			            typename PsimagLite::Vector<RealType>::Type& eigs1,
	                    char option1)
			    : C(C1),
			      eigs(eigs1),
			      option(option1),
			      eigsForGather(C.blocks()),
			      weights(C.blocks())
			{

				for (SizeType m=0;m<C.blocks();m++) {
					eigsForGather[m].resize(C.offsets(m+1)-C.offsets(m));
					weights[m] =  C.offsets(m+1)-C.offsets(m);
				}

				eigs.resize(C.rank());
			}

			void thread_function_(SizeType threadNum,
			                      SizeType blockSize,
			                      SizeType total,
			                      typename PsimagLite::Concurrency::MutexType* myMutex)
			{
				for (SizeType p=0;p<blockSize;p++) {
					SizeType taskNumber = threadNum*blockSize + p;
					if (taskNumber>=total) break;

					SizeType m = taskNumber;
					typename PsimagLite::Vector<RealType>::Type eigsTmp;
					PsimagLite::diag(C.data_[m],eigsTmp,option);
					enforcePhase(C.data_[m]);
					for (int j=C.offsets(m);j< C.offsets(m+1);j++)
						eigsForGather[m][j-C.offsets(m)] = eigsTmp[j-C.offsets(m)];
				}
			}

			template<typename SomeParallelType>
			void gather(SomeParallelType& p)
			{
				if (ConcurrencyType::mode == ConcurrencyType::MPI) {
					p.allGather(eigsForGather);
					p.allGather(C.data_);
				}

				for (SizeType m=0;m<C.blocks();m++) {
					for (int j=C.offsets(m);j< C.offsets(m+1);j++)
						eigs[j]=eigsForGather[m][j-C.offsets(m)];
				}
			}

		private:

			void enforcePhase(T* v,SizeType n)
			{
				T sign1=0;
				for (SizeType j=0;j<n;j++) {
					if (std::norm(v[j])>1e-6) {
						if (std::real(v[j])>0) sign1=1;
						else sign1= -1;
						break;
					}
				}
				// get a consistent phase
				for (SizeType j=0;j<n;j++) v[j] *= sign1;
			}

			void enforcePhase(typename PsimagLite::Vector<T>::Type& v)
			{
				enforcePhase(&(v[0]),v.size());
			}

			void enforcePhase(PsimagLite::Matrix<T>& a)
			{
				T* vpointer = &(a(0,0));
				for (SizeType i=0;i<a.n_col();i++)
					enforcePhase(&(vpointer[i*a.n_row()]),a.n_row());
			}

			BlockMatrixType  &C;
			typename PsimagLite::Vector<RealType>::Type& eigs;
			char option;
			typename PsimagLite::Vector<typename PsimagLite::Vector<RealType>::Type>::Type eigsForGather;
			typename PsimagLite::Vector<SizeType>::Type weights;
		};

		BlockMatrix(int rank,int blocks) : rank_(rank),offsets_(blocks+1),data_(blocks) 
		{
			offsets_[blocks]=rank;
		}

		void operator+=(BlockMatrix<T,MatrixInBlockTemplate> const &m)
		{
			BlockMatrix<T,MatrixInBlockTemplate> c;
			if (offsets_.size()<m.blocks()) operatorPlus(c,*this,m);
			else operatorPlus(c,m,*this);
			*this = c;
		}

		//! Sets all blocks of size 1 and value value
		void makeDiagonal(int n,T const &value)
		{
			rank_ = n;
			MatrixInBlockTemplate m;
			
			setValue(m,1,value);
			
			offsets_.clear();
			data_.clear();
			for (int i=0;i<n;i++) {
				offsets_.push_back(i);
				data_.push_back(m);
			}
			offsets_.push_back(n);
		}

		//! Set block to the zero matrix, 
		void setToZero(int n)
		{
			T value=static_cast<T>(0.0);
			makeDiagonal(n,value);
		}
		
		//! set block to the identity matrix
		void setToIdentity(int n)
		{
			T value=static_cast<T>(1.0);
			makeDiagonal(n,value);
		}
		
		void setBlock(int i,int offset,MatrixInBlockTemplate const &m) { data_[i]=m; offsets_[i]=offset; }
		
		void sumBlock(int i,MatrixInBlockTemplate const &m) { data_[i] += m; }
		
		int rank() const { return rank_; }
		
		int offsets(int i) const { return offsets_[i]; }
		
		SizeType blocks() const { return data_.size(); }
		
		T operator()(int i,int j) const 
		{	
			int k;
			for (k=data_.size()-1;k>=0;k--) if (i>=offsets_[k]) break;
			
			if (j<offsets_[k] || j>=offsets_[k+1]) return static_cast<T>(0.0);
			return data_[k](i-offsets_[k],j-offsets_[k]);
			
		}
		
		
		MatrixInBlockTemplate operator()(int i) const { return data_[i]; }
		
		template<class S,class MatrixInBlockTemplate2>
		friend void operatorPlus(BlockMatrix<S,MatrixInBlockTemplate2>  &C,BlockMatrix<S,MatrixInBlockTemplate2>  const &A,BlockMatrix<S,MatrixInBlockTemplate2> const &B);
		
		template<class S,class MatrixInBlockTemplate2>
		friend void operatorPlus(BlockMatrix<S,MatrixInBlockTemplate2>   &A,BlockMatrix<S,MatrixInBlockTemplate2> const &B);
	
		template<class S,class MatrixInBlockTemplate2>
		friend std::ostream &operator<<(std::ostream &s,BlockMatrix<S,MatrixInBlockTemplate2> const &A);
		
		template<typename S,typename Field>
		friend void diagonalise(BlockMatrix<S,PsimagLite::Matrix<S> >  &C,typename PsimagLite::Vector<Field> ::Type&eigs,char option);
		
	private:
		int rank_; //the rank of this matrix
		typename PsimagLite::Vector<int>::Type offsets_; //starting of diagonal offsets for each block
		typename PsimagLite::Vector<MatrixInBlockTemplate>::Type data_; // data on each block	
	
	}; // class BlockMatrix

	// Companion Functions
	template<class S,class MatrixInBlockTemplate>
	std::ostream &operator<<(std::ostream &s,BlockMatrix<S,MatrixInBlockTemplate> const &A)
	{
		for (SizeType m=0;m<A.blocks();m++) {
			int nrank = A.offsets(m+1)-A.offsets(m);
			s<<"block number "<<m<<" has rank "<<nrank<<"\n";
			s<<A.data_[m];
		}
		return s;
	}

	//C=A+ B where A.offsets is contained in B.offsets
	template<class S,class MatrixInBlockTemplate>
	void operatorPlus(BlockMatrix<S,MatrixInBlockTemplate>  &C,
			  BlockMatrix<S,MatrixInBlockTemplate>  const &A,BlockMatrix<S,MatrixInBlockTemplate> const &B)
	{
		SizeType i;
		int counter=0;
		C.rank_ = A.rank_;
		C.offsets_=A.offsets_;
		C.data_.resize(A.data_.size());
		for (i=0;i<A.data_.size();i++) {
			C.data_[i] = A.data_[i];
		
			while(B.offsets_[counter] <A.offsets_[i+1]) {
				accumulate(C.data_[i],B.data_[counter++]);
				if (counter>=B.offsets_.size()) break;
			}
			if (counter>=B.offsets_.size() && i<A.offsets_.size()-1) throw PsimagLite::RuntimeError("operatorPlus: restriction not met.\n");	
		}
	}

	//A+= B where A.offsets is contained in B.offsets
	template<class S,class MatrixInBlockTemplate>
	void operatorPlus(BlockMatrix<S,MatrixInBlockTemplate>   &A,BlockMatrix<S,MatrixInBlockTemplate> const &B)
	{
		SizeType i;
		int counter=0;

		for (i=0;i<A.data_.size();i++) {
			while(B.offsets_[counter] < A.offsets_[i+1]) {
				accumulate(A.data_[i],B.data_[counter++]);
				if (counter>=B.offsets_.size()) break;
			}
			if (counter>=B.offsets_.size() && i<A.offsets_.size()-1) throw PsimagLite::RuntimeError("operatorPlus: restriction not met.\n");	
		}
	 	
	}

	//! Parallel version of the diagonalization of a block diagonal matrix
	template<typename S,typename SomeVectorType>
	typename PsimagLite::EnableIf<PsimagLite::IsVectorLike<SomeVectorType>::True,
	void>::Type
	diagonalise(BlockMatrix<S,PsimagLite::Matrix<S> >& C,
	            SomeVectorType& eigs,
	            char option)
	{
		typedef typename BlockMatrix<S,PsimagLite::Matrix<S> >::LoopForDiag LoopForDiagType;
		typedef PsimagLite::Parallelizer<LoopForDiagType> ParallelizerType;
		ParallelizerType threadObject(PsimagLite::Concurrency::npthreads,PsimagLite::MPI::COMM_WORLD);

		LoopForDiagType helper(C,eigs,option);

		threadObject.loopCreate(C.blocks(),helper); // FIXME: needs weights

		helper.gather(threadObject);
	}

	template<class S,class MatrixInBlockTemplate>
	bool isUnitary(BlockMatrix<S,MatrixInBlockTemplate> const &B)
	{
		bool flag=true;
		MatrixInBlockTemplate matrixTmp;
		
		for (SizeType m=0;m<B.blocks();m++) {
			matrixTmp = B(m);
			if (!isUnitary(matrixTmp)) flag=false;
		}
		return flag;
	}

	template<class S>
	void blockMatrixToFullMatrix(PsimagLite::Matrix<S> &fm,BlockMatrix<S,PsimagLite::Matrix<S> > const &B)
	{
		int n = B.rank();
		int i,j;
		fm.reset(n,n);
		for (j=0;j<n;j++) for (i=0;i<n;i++) fm(i,j)=B(i,j);
	}

	template<class S>
	void blockMatrixToSparseMatrix(PsimagLite::CrsMatrix<S> &fm,BlockMatrix<S,PsimagLite::Matrix<S> > const &B)
	{
		SizeType n = B.rank();
		fm.resize(n,n);
		SizeType counter=0;
		for (SizeType i=0;i<n;i++) {
			fm.setRow(i,counter);
			for (SizeType j=0;j<n;j++) {
				S val = B(i,j);
				if (std::norm(val)<1e-10) continue;
				fm.pushValue(val);
				fm.pushCol(j);
				counter++;
			}
		}
		fm.setRow(n,counter);
		fm.checkValidity();
	}


} // namespace Dmrg
/*@}*/	

#endif
