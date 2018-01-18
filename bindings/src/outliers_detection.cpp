#include "g2o/core/block_solver.h"
#include "g2o/core/optimization_algorithm_levenberg.h"
#include "g2o/solvers/eigen/linear_solver_eigen.h"
#include "g2o/types/sba/types_six_dof_expmap.h"
#include "g2o/core/robust_kernel_impl.h"
//#include "g2o/solvers/linear_solver_dense.h"
#include "g2o/solvers/dense/linear_solver_dense.h"

#include <opencv2/core/core.hpp>
#include <Eigen/StdVector>

#include "outliers_detection.h"
#include "dep_ba.h"

using namespace std;


Eigen::MatrixXd outliersForLocalBundleAdjustment(Eigen::Ref<Eigen::MatrixXd> keyframes, Eigen::Ref<Eigen::MatrixXd> fixedKeyframes, Eigen::Ref<Eigen::MatrixXd> worldMapPoints, Eigen::Ref<Eigen::MatrixXd> pointsRelation )  {
    //primary keyframe
    KeyFrame primaryKeyframe;
    int primaryKeyframeId = keyframes.row(0)(0) ;
    int secondKeyframeId = keyframes.row(1)(0) ;
    Eigen::MatrixXd primKeyFrame(4, 4);
    //cout << keyframes.row(0) << endl;
    primKeyFrame << keyFrameRowToMatrix(keyframes.row(0) );
    //cout << primKeyFrame << std::endl;
    primaryKeyframe = std::make_pair(std::make_pair(0,keyframes.row(0)(0)) , primKeyFrame);
    
    
    //Prep Data
    //frames with same mappoints as the keyframe
    std::vector<KeyFrame> lLocalKeyFrames;
    
    //add keyframe akka 0
    lLocalKeyFrames.push_back(primaryKeyframe);
    
    for(int n = 1; n < keyframes.rows(); n++) {
        Eigen::MatrixXd currentFrame(4, 4);
        currentFrame << keyFrameRowToMatrix(keyframes.row(n));
        //cout << currentFrame << std::endl;
        //cout << keyframes.row(n)(0) << endl;
        KeyFrame frame = std::make_pair( std::make_pair(n,keyframes.row(n)(0)) , currentFrame);
        lLocalKeyFrames.push_back(frame);
    }
    
    
    //step 2 Local MapPoints seen in Local KeyFrames
    std::vector<MapPoint> lLocalMapPoints;
    std::vector<int> indexUsed;
    for( KeyFrame frame : lLocalKeyFrames)
    {
        vector<MapPoint> vpMPs;
        for(int k = 0; k < pointsRelation.rows(); k++) {
            Eigen::MatrixXd relation(1, pointsRelation.cols());
            //cout << k << endl;
            //cout << pointsRelation.row(k) << endl;
            //cout << frame.first.second << endl;
            relation << pointsRelation.row(k);
            if(relation(1) == frame.first.second ) {
                int currentRelationMapPointKey = relation(0);  //index map
                
                bool found = false;
                for (int indexInArray :indexUsed ) {
                    if (indexInArray == currentRelationMapPointKey ) {
                        found = true;
                        break;
                    }
                }
                
                if (found == false) {
                    for(int m = 0; m < worldMapPoints.rows(); m++) {
                        
                        Eigen::MatrixXd currentPoint(1, 3);
                        //cout << worldMapPoints.row(m) << endl;
                        currentPoint <<  mappointRowToMatrix(worldMapPoints.row(m));
                        //cout << currentPoint(0) << endl;
                        if (worldMapPoints.row(m)(0) == currentRelationMapPointKey) {
                            indexUsed.push_back(currentRelationMapPointKey);
                            
                            MapPoint point = std::make_pair( std::make_pair(m, worldMapPoints.row(m)(0) ) , currentPoint);
                            lLocalMapPoints.push_back( point );
                            break;
                        }
                    }
                }
            }
        }
    }
    
    
    // Fixed Keyframes. Keyframes that see Local MapPoints but that are not Local Keyframes
    vector<KeyFrame> lFixedCameras;
    
    for(int n = 0; n < fixedKeyframes.rows(); n++) {
        Eigen::MatrixXd currentFrame(4, 4);
        currentFrame << keyFrameRowToMatrix(fixedKeyframes.row(n));
        //cout << currentFrame << std::endl;
        KeyFrame frame = std::make_pair( std::make_pair(n,fixedKeyframes.row(n)(0)) , currentFrame);
        lFixedCameras.push_back(frame);
    }
    
    
    ///Optimizer
    
    // Setup optimizer
    g2o::SparseOptimizer optimizer;
    g2o::BlockSolver_6_3::LinearSolverType * linearSolver;
    
    linearSolver = new g2o::LinearSolverEigen<g2o::BlockSolver_6_3::PoseMatrixType>();
    
    g2o::BlockSolver_6_3 * solver_ptr = new g2o::BlockSolver_6_3(linearSolver);
    g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(solver_ptr);
    optimizer.setAlgorithm(solver);
    
    unsigned long maxKFid = 0;
    
    // Set Local KeyFrame vertices
    for(KeyFrame frame : lLocalKeyFrames) {
        KeyFrame pKFi = frame;
        g2o::VertexSE3Expmap * vSE3 = new g2o::VertexSE3Expmap();
        vSE3->setEstimate(toSE3QuatFromMatrix(pKFi.second));
        vSE3->setId(pKFi.first.second);
        if (pKFi.first.second == primaryKeyframeId ) {
            vSE3->setFixed(true);
        } else  if (pKFi.first.second == secondKeyframeId) {
            vSE3->setFixed(true);
        } else {
            vSE3->setFixed(false);
        }
        optimizer.addVertex(vSE3);
        if(pKFi.first.second>maxKFid)
        maxKFid=pKFi.first.second;
    }
    
    // Set Fixed KeyFrame vertices
    for(KeyFrame frame :  lFixedCameras) {
        KeyFrame pKFi = frame;
        g2o::VertexSE3Expmap * vSE3 = new g2o::VertexSE3Expmap();
        vSE3->setEstimate(toSE3QuatFromMatrix(pKFi.second));
        vSE3->setId(pKFi.first.second);
        vSE3->setFixed(true);
        optimizer.addVertex(vSE3);
        if(pKFi.first.second>maxKFid)
        maxKFid=pKFi.first.second;
    }
    
    
    // Set MapPoint vertices
    const int nExpectedSize = (lLocalKeyFrames.size()+lFixedCameras.size())*lLocalMapPoints.size();
    
    
    vector<g2o::EdgeSE3ProjectXYZ*> vpEdgesMono;
    vpEdgesMono.reserve(nExpectedSize);
    
    vector<KeyFrame> vpEdgeKFMono;
    vpEdgeKFMono.reserve(nExpectedSize);
    
    vector<MapPoint> vpMapPointEdgeMono;
    vpMapPointEdgeMono.reserve(nExpectedSize);
    
    vector<g2o::EdgeStereoSE3ProjectXYZ*> vpEdgesStereo;
    vpEdgesStereo.reserve(nExpectedSize);
    
    vector<KeyFrame> vpEdgeKFStereo;
    vpEdgeKFStereo.reserve(nExpectedSize);
    
    vector<MapPoint> vpMapPointEdgeStereo;
    vpMapPointEdgeStereo.reserve(nExpectedSize);
    
    const float thHuberMono = sqrt(5.991);
    const float thHuberStereo = sqrt(7.815);
    
    //optimizer check
    int optimizerCheck = 0;
    
    for(MapPoint point : lLocalMapPoints) {
        MapPoint pMP = point;
        g2o::VertexSBAPointXYZ* vPoint = new g2o::VertexSBAPointXYZ();
        vPoint->setEstimate( toVector3d( pMP.second ));
        int id = pMP.first.second+maxKFid+1;
        vPoint->setId(id);
        vPoint->setMarginalized(true);
        optimizer.addVertex(vPoint);
        
        //cout << point.second << endl;
        const std::vector<std::pair<KeyFrame,int>> observations = getObservationsWithRelation(pMP, lLocalKeyFrames, worldMapPoints, pointsRelation);
        
        
        //Set edges
        for(std::pair<KeyFrame,int> mit :  observations)
        {
            KeyFrame pKFi = mit.first;
            
            //keypoint of mappoint in the frame
            Eigen::Matrix<double,1,2> kpUn;
            Eigen::MatrixXd currentPoint(1, pointsRelation.cols());
            currentPoint << pointsRelation.row(mit.second);
            kpUn  << currentPoint(2),  currentPoint(3);
            
            // Monocular observation
            //            if(pKFi->mvuRight[mit->second]<0)
            //            {
            Eigen::Matrix<double,2,1> obs;
            obs << kpUn(0), kpUn(1);
            
            
            g2o::EdgeSE3ProjectXYZ* e = new g2o::EdgeSE3ProjectXYZ();
            
            e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(id)));
            e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(pKFi.first.second)));
            e->setMeasurement(obs);
            //                const float &invSigma2 = pKFi->mvInvLevelSigma2[kpUn.octave];
            e->setInformation(Eigen::Matrix2d::Identity());
            
            g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
            e->setRobustKernel(rk);
            rk->setDelta(thHuberMono);
            
            e->fx = CameraFx;
            e->fy = CameraFy;
            e->cx = CameraCx;
            e->cy = CameraCy;
            
            optimizerCheck++;
            optimizer.addEdge(e);
            vpEdgesMono.push_back(e);
            vpEdgeKFMono.push_back(pKFi);
            vpMapPointEdgeMono.push_back(pMP);
            //            }
            
        }
    }
    
    if (optimizerCheck < 3 ) {
        Eigen::MatrixXd returnMatrix(0, 0) ;
        return  returnMatrix ;
    }
    
    optimizer.initializeOptimization();
    optimizer.optimize(5);
    
    bool bDoMore= true;
    
    if(bDoMore){
        // Check inlier observations
        for(size_t i=0, iend=vpEdgesMono.size(); i<iend;i++) {
            g2o::EdgeSE3ProjectXYZ* e = vpEdgesMono[i];
            MapPoint pMP = vpMapPointEdgeMono[i];
            
            if(e->chi2()>5.991 || !e->isDepthPositive()) {
                e->setLevel(1);
            }
            
            e->setRobustKernel(0);
        }
        
        // Optimize again without the outliers
        optimizer.initializeOptimization(0);
        optimizer.optimize(10);
    }
    
    vector<pair<KeyFrame,MapPoint> > vToErase;
    vector<float> chiArray; 
    vToErase.reserve(vpEdgesMono.size()+vpEdgesStereo.size());
    
    // Check inlier observations
    for(size_t i=0, iend=vpEdgesMono.size(); i<iend;i++) {
        g2o::EdgeSE3ProjectXYZ* e = vpEdgesMono[i];
        MapPoint pMP = vpMapPointEdgeMono[i];
        
        // if(e->chi2()>5.991 || !e->isDepthPositive())
        // {
            chiArray.push_back(e->chi2());
            KeyFrame pKFi = vpEdgeKFMono[i];
            vToErase.push_back(make_pair(pKFi,pMP));
        // }
    }
    


    int toEraseCount = vToErase.size();
    std::vector<float> eraseElements;


   Eigen::MatrixXd returnMatrix(toEraseCount, 3);

    std::cout << "to erase" << vToErase.size() << std::endl;
    if(!vToErase.empty())
    {
        for(size_t i=0;i<vToErase.size();i++)
        {
            KeyFrame pKFi = vToErase[i].first;
            MapPoint pMPi = vToErase[i].second;

            cout << "keyframe id " << vToErase[i].first.first.second << std::endl;
            cout << "mappoint id " << vToErase[i].second.first.second << std::endl;
            // returnMatrix << (float)vToErase[i].first.first.second , (float)vToErase[i].second.first.second, chiArray[i];

            returnMatrix(i, 0) = (float)vToErase[i].first.first.second;
            returnMatrix(i, 1) = (float)vToErase[i].second.first.second;
            returnMatrix(i, 2) = chiArray[i];
            
            //TODO
            //erase mappoint match for keyframe
            //            pKFi->EraseMapPointMatch(pMPi);
            
            //erase keyframe match for mappoint
            //            pMPi->EraseObservation(pKFi);
        }
    }

    return  returnMatrix;
}