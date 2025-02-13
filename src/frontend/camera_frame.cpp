#include <include/camera_frame.h>

CameraFrame::CameraFrame()
{
    this->solving_time = 1;
}

void CameraFrame::clear()
{
    T_c_w = SE3(Quaterniond(1,0,0,0),Vec3(0,0,0));
    img0.release();
    img1.release();
    d_img.release();
    landmarks.clear();
}

void CameraFrame::updateLMT_c_w()
{
    for (auto lm = landmarks.begin(); lm != landmarks.end(); ++lm)
    {
        lm->lm_frame_pose=T_c_w;
    }
}

void CameraFrame::eraseReprjOutlier()
{
    for(int i=this->landmarks.size()-1; i>=0; i--)
    {
        LandMarkInFrame lm=landmarks.at(i);
        if(lm.lm_tracking_state==LM_TRACKING_OUTLIER)
        {
            landmarks.erase(landmarks.begin()+i);
        }
    }
}

void CameraFrame::recover3DPts_c_FromStereo(vector<Vec3> &pt3ds, vector<Vec2>& pts2d_img0, vector<Vec2>& pts2d_img1,
                                            vector<bool> &maskHas3DInf)
{
    pt3ds.clear();
    maskHas3DInf.clear();

    pts2d_img0 = this->get2dPtsVec();
    vector<float>   err;
    vector<unsigned char> status;
    vector<cv::Point2f> pts0 = vVec2_2_vcvP2f(pts2d_img0);
    vector<cv::Point2f> pts1 = pts0;
    for(int i=0; i<this->landmarks.size(); i++)
    {   //reporject lm to cam1
        if(landmarks.at(i).hasDepthInf()){
            Vec3 lm3d_c = StereoCamera::world2cameraT_c_w(landmarks.at(i).lm_3d_w,
                                                         this->d_camera.T_cam1_cam0*this->T_c_w);
            Vec2 reProj=this->d_camera.camera2pixel(lm3d_c,
                                                    this->d_camera.cam1_fx,
                                                    this->d_camera.cam1_fy,
                                                    this->d_camera.cam1_cx,
                                                    this->d_camera.cam1_cy);
            pts1.at(i) = cv::Point2f(reProj[0],reProj[1]);
        }
    }
    cv::calcOpticalFlowPyrLK(this->img0, this->img1,
                             pts0, pts1,
                             status, err, cv::Size(31,31),10,
                             cv::TermCriteria(cv::TermCriteria::COUNT+cv::TermCriteria::EPS, 30, 0.01),
                             cv::OPTFLOW_USE_INITIAL_FLOW);
    pts2d_img0.clear();
    pts2d_img1.clear();
    for(int i=0; i<status.size(); i++)
    {
        pts2d_img0.push_back(Vec2(pts0.at(i).x,pts0.at(i).y));
        pts2d_img1.push_back(Vec2(pts1.at(i).x,pts1.at(i).y));
    }

    for(size_t i=0; i<status.size(); i++)
    {
        if(status.at(i)==1)
        {
            Vec3 pt3d_c;
            if(Triangulation::trignaulationPtFromStereo(pts2d_img0.at(i),pts2d_img1.at(i),
                                                        this->d_camera.P0_,
                                                        this->d_camera.P1_,
                                                        pt3d_c))
            {
                pt3ds.push_back(pt3d_c);
                maskHas3DInf.push_back(true);
                continue;
            }
            else
            {
                pt3ds.push_back(Vec3(0,0,0));
                maskHas3DInf.push_back(false);
                continue;
            }


        }else
        {
            //stereo optical flow matching fail,
            //The point may too close to camera
            //use dummy depth writing technology
            Vec3 pt3d_c;
            float d_rand;
//            d_rand = 0.5;
            d_rand = 0.3 + static_cast<float>(rand())/(static_cast<float>(RAND_MAX/(0.4)));
            pt3d_c = StereoCamera::pixel2camera(pts2d_img0.at(i),
                                               this->d_camera.cam0_fx,
                                               this->d_camera.cam0_fy,
                                               this->d_camera.cam0_cx,
                                               this->d_camera.cam0_cy,
                                               d_rand);
            pt3ds.push_back(pt3d_c);
            maskHas3DInf.push_back(true);
            continue;
        }
    }
}

void CameraFrame::recover3DPts_c_FromTriangulation(vector<Vec3> &pt3ds, vector<bool> &maskHas3DInf)
{
    pt3ds.clear();
    maskHas3DInf.clear();
    for(size_t i=0; i<landmarks.size(); i++)
    {
        SE3 T_c_w1=landmarks.at(i).lm_1st_obs_frame_pose;
        Vec3 baseline = T_c_w1.translation()-T_c_w.translation();
        if(baseline.norm()>=0.2)
        {
            Vec3 pt3d_w = Triangulation::triangulationPt(landmarks.at(i).lm_1st_obs_2d,landmarks.at(i).lm_2d,
                                                         landmarks.at(i).lm_1st_obs_frame_pose,T_c_w,
                                                         d_camera.cam0_fx,
                                                         d_camera.cam0_fy,
                                                         d_camera.cam0_cx,
                                                         d_camera.cam0_cy);
            Vec3 pt3d_c = StereoCamera::world2cameraT_c_w(pt3d_w,T_c_w);
            if(pt3d_c[2]>=0.5 && pt3d_c[2]<=15)
            {
                pt3ds.push_back(pt3d_c);
                maskHas3DInf.push_back(true);
            }
            else
            {
                pt3ds.push_back(Vec3(0,0,0));
                maskHas3DInf.push_back(false);
            }
        }
        else
        {
            pt3ds.push_back(Vec3(0,0,0));
            maskHas3DInf.push_back(false);
        }
    }
}

void CameraFrame::correctLMP3DWByLMP3DCandT(void)
{
    SE3 T=this->T_c_w;
    for(auto lm:landmarks)
    {
        if(lm.hasDepthInf())
        {
            lm.lm_3d_w = StereoCamera::camera2worldT_c_w(lm.lm_3d_c,T);
        }
    }
}

void CameraFrame::forceCorrectLM3DW(const int &cnt, const vector<int64_t> &ids, const vector<Vec3> &lms_3d)
{
    if(cnt<=0) return;
    for(int i=0; i<cnt; i++)
    {
        int correct_lm_id=ids.at(i);
        for(size_t j=0; j<landmarks.size(); j++)
        {
            if(landmarks.at(j).lm_id==correct_lm_id)
            {
                landmarks.at(j).lm_3d_w=lms_3d.at(i);
                break;
            }
        }
    }
}

void CameraFrame::forceMarkOutlier(const int &cnt, const vector<int64_t> &ids)
{
    if(cnt<=0) return;
    for(int i=0; i<cnt; i++)
    {
        int correct_lm_id=ids.at(i);
        for(size_t j=0; j<landmarks.size(); j++)
        {
            if(landmarks.at(j).lm_id==correct_lm_id)
            {
                //                cout <<"match!" << endl;
                landmarks.at(j).lm_tracking_state=LM_TRACKING_OUTLIER;
            }
        }
    }
}

int CameraFrame::validLMCount()
{
    int ret=0;
    if(landmarks.size()>0)
    {
        for(size_t i=0; i<landmarks.size(); i++)
        {
            if(landmarks.at(i).hasDepthInf()&&(landmarks.at(i).lm_tracking_state==LM_TRACKING_INLIER))
            {
                ret++;
            }
        }
    }
    return ret;
}

void CameraFrame::getValid2d3dPair_cvPf(vector<cv::Point2f> &p2d, vector<cv::Point3f> &p3d)
{
    p2d.clear();
    p3d.clear();
    for(size_t i=0; i<landmarks.size(); i++)
    {
        LandMarkInFrame lm=landmarks.at(i);
        if(lm.hasDepthInf() && lm.lm_tracking_state==LM_TRACKING_INLIER)
        {
            p2d.push_back(cv::Point2f(lm.lm_2d[0],lm.lm_2d[1]));
            p3d.push_back(cv::Point3f(lm.lm_3d_w[0],lm.lm_3d_w[1],lm.lm_3d_w[2]));
        }
    }
}

void CameraFrame::updateLMState(vector<uchar> status)
{
    int indexLM = 0;
    for(size_t i=0; i<landmarks.size(); i++)
    {
        LandMarkInFrame lm=landmarks.at(i);
        if(lm.hasDepthInf() && lm.lm_tracking_state==LM_TRACKING_INLIER)
        {
            if(status[indexLM] == 0)
                landmarks[i].lm_tracking_state = LM_TRACKING_OUTLIER;
            indexLM += 1;
        }
    }

    //cout<<status.size()<<" compare "<<indexLM<<endl;

}
void CameraFrame::getValidInliersPair(vector<LandMarkInFrame> &lms)
{
    lms.clear();
    for(size_t i=0; i<landmarks.size(); i++)
    {
        LandMarkInFrame lm=landmarks.at(i);
        if(lm.hasDepthInf() && lm.lm_tracking_state==LM_TRACKING_INLIER)
        {
            lms.push_back(lm);
        }
    }
}

void CameraFrame::unpack(vector<Vec2> &pt2d,
                         vector<Vec3> &pt3d,
                         vector<unsigned char> &mask3d)
{
    pt2d.clear();
    // descriptors.clear();
    pt3d.clear();
    mask3d.clear();
    for(size_t i=0; i<pt2d.size(); i++)
    {
        pt2d.push_back(landmarks.at(i).lm_2d);
        pt3d.push_back(landmarks.at(i).lm_3d_w);
        // descriptors.push_back(landmarks.at(i).lm_descriptor);
        if(landmarks.at(i).hasDepthInf())
        {mask3d.push_back(1);}
        else
        {mask3d.push_back(0);}
    }
}

vector<Vec3> CameraFrame::getValid3dPts(void)
{
    vector<Vec3> ret;
    for(size_t i=0; i<landmarks.size(); i++)
    {
        LandMarkInFrame lm=landmarks.at(i);
        if(lm.hasDepthInf())
        {
            ret.push_back(lm.lm_3d_w);
        }
    }
    return ret;

}

vector<cv::Point2f> CameraFrame::get2dPtsVec_cvP2f(void)
{
    vector<cv::Point2f> ret;
    ret.clear();
    for(size_t i=0; i<landmarks.size(); i++)
    {
        ret.push_back(cv::Point2f(landmarks.at(i).lm_2d[0],
                      landmarks.at(i).lm_2d[1]));
    }
    return ret;
}
vector<cv::Point3f> CameraFrame::get3dPtsVec_cvP3f(void)
{
    vector<cv::Point3f> ret;
    ret.clear();
    for(size_t i=0; i<landmarks.size(); i++)
    {
        ret.push_back(cv::Point3f(landmarks.at(i).lm_2d[0],
                      landmarks.at(i).lm_2d[1],
                landmarks.at(i).lm_2d[3]));
    }
    return ret;
}

vector<Vec2> CameraFrame::get2dPtsVec(void)
{
    vector<Vec2> ret;
    ret.clear();
    for(size_t i=0; i<landmarks.size(); i++)
    {
        ret.push_back(landmarks.at(i).lm_2d);
    }
    return ret;
}

vector<Vec3> CameraFrame::get3dPtsVec(void)
{
    vector<Vec3> ret;
    ret.clear();
    for(size_t i=0; i<landmarks.size(); i++)
    {
        ret.push_back(landmarks.at(i).lm_3d_w);
    }
    return ret;
}

// vector<cv::Mat>  CameraFrame::getDescriptorVec(void)
// {
//     vector<cv::Mat> ret;
//     ret.clear();
//     for(size_t i=0; i<landmarks.size(); i++)
//     {
//         ret.push_back(landmarks.at(i).lm_descriptor);
//     }
//     return ret;
// }

void CameraFrame::getKeyFrameInf(vector<int64_t> &lm_id, vector<Vec2> &lm_2d, vector<Vec3> &lm_3d)
{
    lm_id.clear();
    lm_2d.clear();
    lm_3d.clear();
    // lm_descriptors.clear();
    for(size_t i=0; i<landmarks.size(); i++)
    {
        LandMarkInFrame lm=landmarks.at(i);
        if(lm.hasDepthInf())
        {
            lm_3d.push_back(lm.lm_3d_w);
            lm_2d.push_back(lm.lm_2d);
            lm_id.push_back(lm.lm_id);
            // lm_descriptors.push_back(lm.lm_descriptor);
        }
    }

}
