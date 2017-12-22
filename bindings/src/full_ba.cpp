#include "g2o/core/block_solver.h"
#include "g2o/core/optimization_algorithm_levenberg.h"
#include "g2o/solvers/eigen/linear_solver_eigen.h"
#include "g2o/types/sba/types_six_dof_expmap.h"
#include "g2o/core/robust_kernel_impl.h"
//#include "g2o/solvers/linear_solver_dense.h"
#include "g2o/solvers/dense/linear_solver_dense.h"

#include <opencv2/core/core.hpp>
#include <Eigen/StdVector>

#include "local_ba.h"

using namespace std;

// column size for keyframe
typedef Eigen::Matrix<double, 4, 4> KeyFrameMatrix;
typedef Eigen::Matrix<double, 1, 3> MapPointMatrix;
typedef Eigen::Matrix<double, 1, 4> KeyFrameMapPointMatrix;

//index - key
typedef std::pair<  std::pair<int, int>, KeyFrameMatrix> KeyFrame;
typedef std::pair< std::pair<int, int>, MapPointMatrix> MapPoint;

const float CameraFx = 718.856;
const float CameraFy = 718.856;
const float CameraCx = 607.1928;
const float CameraCy = 185.2157;

Eigen::Matrix<double,3,1> toVector3d(const MapPointMatrix point){
    Eigen::Matrix<double,3,1> v;
    v << point(0), point(1), point(2);
    
    return v;
}

g2o::SE3Quat toSE3QuatFromMatrix(const Eigen::Matrix<double,4,4> &eigenMat){
    Eigen::Matrix<double,3,3> R;
    R << eigenMat(0,0), eigenMat(0,1), eigenMat(0,2),
    eigenMat(1,0), eigenMat(1,1), eigenMat(1,2),
    eigenMat(2,0), eigenMat(2,1), eigenMat(2,2);
    
    Eigen::Matrix<double,3,1> t(eigenMat(0,3), eigenMat(1,3), eigenMat(2,3));
    
    return g2o::SE3Quat(R,t);
}

Eigen::MatrixXd toEigenBundel(const g2o::SE3Quat &SE3){
    Eigen::Matrix<double, 4, 4> eigMat = SE3.to_homogeneous_matrix();
    return eigMat;
}


Eigen::MatrixXd toEigenVector(const Eigen::Matrix<double,3,1> &m)
{
    Eigen::Matrix<double, 1, 3> eigMat;
    eigMat << m(0,0), m(1,0), m(2,0);
    return eigMat;
}


Eigen::MatrixXd keyFrameRowToMatrix(const Eigen::MatrixXd row)
{
    Eigen::MatrixXd frameMtrix(4, 4);
    
    frameMtrix << row(1), row(2), row(3), row(4),
    row(5), row(6), row(7), row(8),
    row(9), row(10), row(11), row(12),
    row(13), row(14), row(15), row(16);
    
    return frameMtrix;
}

Eigen::MatrixXd mappointRowToMatrix(const Eigen::MatrixXd row)
{
    Eigen::MatrixXd pointMatrix(1, 3);
    pointMatrix << row(1), row(2), row(3) ;
    return pointMatrix;
}


std::vector<KeyFrame> getObservations(MapPoint point ,  std::vector<KeyFrame> keyframes,  Eigen::MatrixXd worldMapPoints,Eigen::MatrixXd pointsRelation) {
    std::vector<KeyFrame> returnKeyframes;
    for(int r = 0; r <  pointsRelation.rows(); r++) {
        Eigen::MatrixXd currentRelation(1, pointsRelation.cols());
        currentRelation << pointsRelation.row(r);
        if (point.first.second == currentRelation(0)) {
            //found relation
            for (KeyFrame frame : keyframes) {
                if (currentRelation(1) == frame.first.second) {
                    returnKeyframes.push_back(frame);
                    break;
                }
            }
        }
    }
    
    return returnKeyframes;
}

std::vector<std::pair<KeyFrame, int>> getObservationsWithRelation(MapPoint point ,  std::vector<KeyFrame> keyframes,  Eigen::MatrixXd, Eigen::MatrixXd pointsRelation ) {
    std::vector<std::pair<KeyFrame, int>> returnKeyframes;
    for(int r = 0; r <  pointsRelation.rows(); r++) {
        Eigen::MatrixXd currentRelation(1, pointsRelation.cols());
        currentRelation << pointsRelation.row(r);
        //cout << currentRelation << endl;
        //cout << point.first.second << endl;
        if (point.first.second == currentRelation(0)) {
            //found relation
            for (KeyFrame frame : keyframes) {
                if (currentRelation(1) == frame.first.second) {
                    
                    returnKeyframes.push_back(std::make_pair(frame,r) );
                    break;
                }
            }
        }
    }
    
    return returnKeyframes;
}

int fullBundleAdjustment(Eigen::Ref<Eigen::MatrixXd> keyframes, Eigen::Ref<Eigen::MatrixXd> mapPoints, Eigen::Ref<Eigen::MatrixXd> pointsRelation )  {
       //step 1 setup keyframes
    std::vector<KeyFrame> vpKFs;
    for(int n = 0; n < keyframes.rows(); n++) {
        Eigen::MatrixXd currentFrame(4, 4);
        currentFrame << keyFrameRowToMatrix(keyframes.row(n));
        cout << currentFrame << std::endl;
        cout << keyframes.row(n)(0) << endl;
        KeyFrame frame = std::make_pair( std::make_pair(n,keyframes.row(n)(0)) , currentFrame);
        vpKFs.push_back(frame);
    }
    
    //step 2 setup mappoints
    std::vector<MapPoint> vpMP;
    for(int n = 0; n < mapPoints.rows(); n++) {
        Eigen::MatrixXd currentPoint(1, 3);
        cout << mapPoints.row(n) << endl;
        currentPoint <<  mappointRowToMatrix(mapPoints.row(n));
        cout << currentPoint(0) << endl;
        MapPoint point = std::make_pair( std::make_pair(n, mapPoints.row(n)(0) ) , currentPoint);
        vpMP.push_back(point );
    }
    
    //mappoints to skip
    vector<bool> vbNotIncludedMP;
    vbNotIncludedMP.resize(vpMP.size());
    
    //Step 3 Setup optimizer
    g2o::SparseOptimizer optimizer;
    g2o::BlockSolver_6_3::LinearSolverType * linearSolver;
    
    linearSolver = new g2o::LinearSolverEigen<g2o::BlockSolver_6_3::PoseMatrixType>();
    
    g2o::BlockSolver_6_3 * solver_ptr = new g2o::BlockSolver_6_3(linearSolver);
    g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(solver_ptr);
    optimizer.setAlgorithm(solver);
    
    
    
    unsigned long maxKFid = 0;
    
    //Step 4 Set KeyFrame vertices
    for(int i = 0; i < vpKFs.size(); i++) {
        KeyFrame pKFi = vpKFs[i];
        g2o::VertexSE3Expmap * vSE3 = new g2o::VertexSE3Expmap();
        vSE3->setEstimate(toSE3QuatFromMatrix(pKFi.second));
        vSE3->setId(pKFi.first.second);
        if (pKFi.first.second == 0 ) {
            vSE3->setFixed(true);
        } else {
            vSE3->setFixed(false);
        }
        optimizer.addVertex(vSE3);
        if(pKFi.first.second>maxKFid)
        maxKFid=pKFi.first.second;
    }
    
    
    const float thHuberMono = sqrt(5.991);
    const float thHuberStereo = sqrt(7.815);
    
    //optimizer check
    int optimizerCheck = 0;
    
    for(int i = 0; i < vpMP.size(); i++) {
        MapPoint pMP = vpMP[i];
        g2o::VertexSBAPointXYZ* vPoint = new g2o::VertexSBAPointXYZ();
        vPoint->setEstimate( toVector3d( pMP.second ));
        int id = pMP.first.second+maxKFid+1;
        vPoint->setId(id);
        vPoint->setMarginalized(true);
        optimizer.addVertex(vPoint);
        
        
        cout << pMP.second << endl;
        const std::vector<std::pair<KeyFrame,int>> observations = getObservationsWithRelation(pMP, vpKFs, mapPoints, pointsRelation);
        
        
        //SET EDGES
        int nEdges = 0;
        for(std::pair<KeyFrame,int> mit :  observations) {
            nEdges++;
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
            
            
        }
        
        if(nEdges==0)
        {
            optimizer.removeVertex(vPoint);
            vbNotIncludedMP[i]=true;
        }
        else
        {
            vbNotIncludedMP[i]=false;
        }
    }
    
    
    if (optimizerCheck < 3 ) {
        return  0;
    }
    
    
    // Optimize!
    int nIterations = 10;
    optimizer.initializeOptimization();
    optimizer.optimize(nIterations);
    
    
    // Recover optimized data
    
    //Keyframes
    //TODO: nLoopKF check base code
    for(int i=0; i<vpKFs.size(); i++) {
        KeyFrame pKF = vpKFs[i];
        g2o::VertexSE3Expmap* vSE3 = static_cast<g2o::VertexSE3Expmap*>(optimizer.vertex(pKF.first.second));
        g2o::SE3Quat SE3quat = vSE3->estimate();
        pKF.second = toEigen(SE3quat);
        cout<< pKF.second << endl;
        keyframes(pKF.first.first, 1) = pKF.second(0, 0);
        keyframes(pKF.first.first, 2) = pKF.second(0, 1);
        keyframes(pKF.first.first, 3) = pKF.second(0, 2);
        keyframes(pKF.first.first, 4) = pKF.second(0, 3);
        keyframes(pKF.first.first, 5) = pKF.second(1, 0);
        keyframes(pKF.first.first, 6) = pKF.second(1, 1);
        keyframes(pKF.first.first, 7) = pKF.second(1, 2);
        keyframes(pKF.first.first, 8) = pKF.second(1, 3);
        keyframes(pKF.first.first, 9) = pKF.second(2, 0);
        keyframes(pKF.first.first, 10) = pKF.second(2, 1);
        keyframes(pKF.first.first, 11) = pKF.second(2, 2);
        keyframes(pKF.first.first, 12) = pKF.second(2, 3);
        keyframes(pKF.first.first, 13) = pKF.second(3, 0);
        keyframes(pKF.first.first, 14) = pKF.second(3, 1);
        keyframes(pKF.first.first, 15) = pKF.second(3, 2);
        keyframes(pKF.first.first, 16) = pKF.second(3, 3);
    }
    
    //Points
    //TODO: nLoopKF check base code
    for(int i=0; i<vpMP.size(); i++) {
        if(vbNotIncludedMP[i])
        continue;
        MapPoint pMP = vpMP[i];
        g2o::VertexSBAPointXYZ* vPoint = static_cast<g2o::VertexSBAPointXYZ*>(optimizer.vertex(pMP.first.second+maxKFid+1));
        pMP.second = toEigenVector(vPoint->estimate()) ;
        //        pMP->UpdateNormalAndDepth();
    }
    cout << "done" << endl;
    return 1;
}