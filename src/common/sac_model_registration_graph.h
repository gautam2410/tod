/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2009, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef SAC_MODEL_REGISTRATION_GRAPH_H_
#define SAC_MODEL_REGISTRATION_GRAPH_H_

#include <opencv2/core/core.hpp>

#include "sac_model.h"

#include "maximum_clique.h"

namespace tod
{
  /**
   * Class that computes the registration between two point clouds in the specific case where we have an adjacency graph
   * (and some points cannot be connected together)
   */
  class SampleConsensusModelRegistrationGraph: public pcl::SampleConsensusModel
  {
    using pcl::SampleConsensusModel::indices_;
    using pcl::SampleConsensusModel::shuffled_indices_;

  public:
    using pcl::SampleConsensusModel::drawIndexSample;
    typedef unsigned int Index;
    typedef std::vector<Index> IndexVector;

    /** \brief Constructor for base SampleConsensusModelRegistration.
     * \param cloud the input point cloud dataset
     * \param indices a vector of point indices to be used from \a cloud
     */
    SampleConsensusModelRegistrationGraph(const std::vector<cv::Vec3f> &query_points,
                                          const std::vector<cv::Vec3f> &target, const IndexVector &indices,
                                          float threshold, const maximum_clique::AdjacencyMatrix & physical_adjacency,
                                          const maximum_clique::AdjacencyMatrix &sample_adjacency)
        :
          physical_adjacency_(physical_adjacency),
          sample_adjacency_(sample_adjacency),
          best_inlier_number_(0),
          threshold_(threshold)
    {
      indices_ = indices;
      shuffled_indices_ = indices;
      query_points_ = query_points;
      training_points_ = target;
    }

    bool
    drawIndexSampleHelper(IndexVector & valid_samples, unsigned int n_samples, IndexVector & samples) const
    {
      if (n_samples == 0)
        return true;
      if (valid_samples.empty())
        return false;
      while (true)
      {
        int sample = valid_samples[rand() % valid_samples.size()];
        IndexVector new_valid_samples(valid_samples.size());
        IndexVector::iterator end = std::set_intersection(valid_samples.begin(), valid_samples.end(),
                                                               sample_adjacency_.neighbors(sample).begin(),
                                                               sample_adjacency_.neighbors(sample).end(),
                                                               new_valid_samples.begin());
        new_valid_samples.resize(end - new_valid_samples.begin());
        IndexVector new_samples;
        if (drawIndexSampleHelper(new_valid_samples, n_samples - 1, new_samples))
        {
          samples = new_samples;
          valid_samples = new_valid_samples;
          samples.push_back(sample);
          return true;
        }
        else
        {
          IndexVector::iterator end = std::remove(valid_samples.begin(), valid_samples.end(), sample);
          valid_samples.resize(end - valid_samples.begin());
          if (valid_samples.empty())
            return false;
        }
      }
      return false;
    }

    bool
    isSampleGood(const IndexVector &samples) const
    {
      IndexVector valid_samples = indices_;
      IndexVector &new_samples = const_cast<IndexVector &>(samples);
      size_t sample_size = new_samples.size();
      bool is_good = drawIndexSampleHelper(valid_samples, sample_size, new_samples);

      if (is_good)
        samples_ = new_samples;

      return is_good;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  void
  selectWithinDistance(const cv::Matx33f &R, const cv::Vec3f&T, double threshold, IndexVector &in_inliers)
  {
    IndexVector inliers;

    double thresh = threshold * threshold;

      inliers.resize(indices_.size());

      int nr_p = 0;
      for (size_t i = 0; i < indices_.size(); ++i)
      {
      const cv::Vec3f & pt_src = query_points_[indices_[i]];
      const cv::Vec3f & pt_tgt = training_points_[indices_[i]];
      cv::Vec3f p_tr  = R * pt_src + T;
      // Calculate the distance from the transformed point to its correspondence
      if (cv::norm(p_tr - pt_tgt)*cv::norm(p_tr-pt_tgt) < thresh)
        inliers[nr_p++] = indices_[i];
    }
    inliers.resize (nr_p);






    in_inliers.clear();
    // Make sure the sample belongs to the inliers
    BOOST_FOREACH(int sample, samples_)
    if (std::find(inliers.begin(), inliers.end(), sample) == inliers.end())
    return;

    // Remove all the points that cannot belong to a clique including the samples
    BOOST_FOREACH(int inlier, inliers)
    {
      bool is_good = true;
      BOOST_FOREACH(int sample, samples_)
      {
        if (sample == inlier)
        break;
        if (!physical_adjacency_.test(inlier, sample))
        {
          is_good = false;
          break;
        }
      }
      if (is_good)
      in_inliers.push_back(inlier);
    }

    // If that set is not bigger than the best so far, no need to refine it
    if (in_inliers.size() < best_inlier_number_)
    return;

    maximum_clique::Graph graph(in_inliers.size());
    for (unsigned int j = 0; j < in_inliers.size(); ++j)
    for (unsigned int i = j + 1; i < in_inliers.size(); ++i)
    if (sample_adjacency_.test(in_inliers[j], in_inliers[i]))
    graph.AddEdgeSorted(j, i);

    // If we cannot even find enough points well distributed in the sample, stop here
    unsigned int minimal_size = 8;
    std::vector<unsigned int> vertices;
    graph.FindClique(vertices, minimal_size);
    if (vertices.size() < minimal_size)
    {
      in_inliers.clear();
      return;
    }

    best_inlier_number_ = std::max(in_inliers.size(), best_inlier_number_);
  }

    bool
    computeModelCoefficients(const IndexVector &samples, cv::Matx33f &R, cv::Vec3f&T)
    {
      // Need 3 samples
      if (samples.size() != 3)
        return (false);

      return estimateRigidTransformationSVD(samples, R, T);
    }

    void
    optimizeModelCoefficients(const IndexVector &inliers, cv::Matx33f&R, cv::Vec3f&T)
    {
      estimateRigidTransformationSVD(inliers, R, T);
    }

    mutable IndexVector samples_;

    /** \brief Estimate a rigid transformation between a source and a target point cloud using an SVD closed-form
     * solution of absolute orientation using unit quaternions
     * \param[in] cloud_src the source point cloud dataset
     * \param[in] indices_src the vector of indices describing the points of interest in cloud_src
     * \param[in] cloud_tgt the target point cloud dataset
     * \param[out] transform the resultant transformation matrix (as model coefficients)
     *
     * This method is an implementation of: Horn, B. “Closed-Form Solution of Absolute Orientation Using Unit Quaternions,” JOSA A, Vol. 4, No. 4, 1987
     * THIS IS COPIED STRAIGHT UP FROM PCL AS THEY CHANGED THE API ANDMADE IT PRIVATE
     */
    bool
    estimateRigidTransformationSVD(const IndexVector &indices_src, cv::Matx33f &R_in, cv::Vec3f&T)
    {
      if (indices_src.size() < 3)
        return false;

      cv::Vec3f centroid_training(0, 0, 0), centroid_query(0, 0, 0);

      // Estimate the centroids of source, target
      BOOST_FOREACH(Index index, indices_src)
      {
        centroid_training += training_points_[index];
        centroid_query += query_points_[index];
      }
      centroid_training /= float(indices_src.size());
      centroid_query /= float(indices_src.size());

      // Subtract the centroids from source, target
      cv::Mat_<cv::Vec3f> sub_training(indices_src.size(),1), sub_query(indices_src.size(),1);
      unsigned int i = 0;
      BOOST_FOREACH(Index index, indices_src)
      {
        sub_training(i) = training_points_[index] - centroid_training;
        sub_query(i) = query_points_[index] - centroid_query;
        ++i;
      }

      // Assemble the correlation matrix
      cv::Mat H = sub_training.reshape(1, indices_src.size()).t() * sub_query.reshape(1, indices_src.size());

      // Compute the Singular Value Decomposition
      cv::SVD svd(H);

      // Compute R = U * V'
      cv::Mat_<float> vt = cv::Mat(svd.vt);
      if (cv::determinant(svd.u) * cv::determinant(vt) < 0)
      {
        for (int x = 0; x < 3; ++x)
        vt(2, x) *= -1;
      }

      R_in = cv::Mat(svd.u * vt);
      T = centroid_training - R_in * centroid_query;

      // Make sure the sample do verify the transform
      /*BOOST_FOREACH(Index sample, samples){
       if (distSq(R*training_points_[sample] + T, query_points_[sample])>threshold*threshold)
       return false;
       }*/

      return true;
    }

  private:

  const maximum_clique::AdjacencyMatrix physical_adjacency_;
  const maximum_clique::AdjacencyMatrix sample_adjacency_;
  size_t best_inlier_number_;
  float threshold_;

    std::vector<cv::Vec3f> query_points_;
    std::vector<cv::Vec3f> training_points_;
  };
}

#endif
