#include "alego/utility.h"

using namespace std;

class IP
{
private:
  ros::NodeHandle nh_;
  ros::Subscriber sub_pc_;
  ros::Publisher pub_segmented_cloud_;
  ros::Publisher pub_seg_info_;
  ros::Publisher pub_outlier_;
  ros::Publisher pub_diagnostics_;
  sensor_msgs::PointCloud2Ptr segmented_cloud_msg_;
  alego::cloud_infoPtr seg_info_msg_;
  sensor_msgs::PointCloud2Ptr outlier_cloud_msg_;
  diagnostic_msgs::DiagnosticArray diagnostic_msg_;
  ros::Timer timer_;
  ros::Time last_cloud_msg_stamp_;
  bool lidar_params_validity_;

  PointCloudT::Ptr full_cloud_;
  PointCloudT::Ptr segmented_cloud_;
  PointCloudT::Ptr outlier_cloud_;

  Eigen::Matrix<double, Eigen::Dynamic, Horizon_SCAN> range_mat_;
  Eigen::Matrix<int, Eigen::Dynamic, Horizon_SCAN> label_mat_;
  Eigen::Matrix<bool, Eigen::Dynamic, Horizon_SCAN> ground_mat_;

  int label_cnt_;
  vector<pair<int, int>> neighbor_iter_;

  // params

public:
  IP(ros::NodeHandle nh) : nh_(nh)
  {
    onInit();
  }

  void onInit()
  {
    if (debug_output) {
      ROS_INFO("---------- ImageProjection init -----------");
    }
    TicToc t_init;

    segmented_cloud_msg_.reset(new sensor_msgs::PointCloud2());
    seg_info_msg_.reset(new alego::cloud_info());
    outlier_cloud_msg_.reset(new sensor_msgs::PointCloud2());

    seg_info_msg_->startRingIndex.resize(N_SCAN);
    seg_info_msg_->endRingIndex.resize(N_SCAN);
    seg_info_msg_->segmentedCloudColInd.assign(N_SCAN * Horizon_SCAN, false);
    seg_info_msg_->segmentedCloudGroundFlag.assign(N_SCAN * Horizon_SCAN, 0);
    seg_info_msg_->segmentedCloudRange.assign(N_SCAN * Horizon_SCAN, 0);
    full_cloud_.reset(new PointCloudT());
    segmented_cloud_.reset(new PointCloudT());
    outlier_cloud_.reset(new PointCloudT());
    PointT nan_p;
    nan_p.intensity = -1;
    full_cloud_->points.resize(N_SCAN * Horizon_SCAN);
    std::fill(full_cloud_->points.begin(), full_cloud_->points.end(), nan_p);

    range_mat_.resize(N_SCAN, Eigen::NoChange);
    label_mat_.resize(N_SCAN, Eigen::NoChange);
    ground_mat_.resize(N_SCAN, Eigen::NoChange);
    range_mat_.setConstant(std::numeric_limits<double>::max());
    label_mat_.setZero();
    ground_mat_.setZero();
    label_cnt_ = 1;
    neighbor_iter_.emplace_back(-1, 0);
    neighbor_iter_.emplace_back(1, 0);
    neighbor_iter_.emplace_back(0, -1);
    neighbor_iter_.emplace_back(0, 1);

    diagnostic_msg_.header.frame_id = "";
    diagnostic_msg_.header.stamp = ros::Time::now();
    diagnostic_msg_.status.resize(2);
    char hostname[20];
    gethostname(hostname, 20);
    for (int i = 0; i < diagnostic_msg_.status.size(); i++)
    {
        diagnostic_msg_.status[i].level = diagnostic_msgs::DiagnosticStatus::STALE;
        diagnostic_msg_.status[i].message = "No data";
        diagnostic_msg_.status[i].hardware_id = std::string(hostname);
    }
    diagnostic_msgs::KeyValue kv;
    kv.key = "Stamp";        kv.value = "No messages";
    diagnostic_msg_.status[0].name = "Parameters";
    diagnostic_msg_.status[1].name = "PointCloud";
    diagnostic_msg_.status[1].values.push_back(kv);

    
    last_cloud_msg_stamp_ = ros::Time(0);
    lidar_params_validity_ = false;

    pub_segmented_cloud_ = nh_.advertise<sensor_msgs::PointCloud2>("/segmented_cloud", 10);
    pub_seg_info_ = nh_.advertise<alego::cloud_info>("/seg_info", 10);
    pub_outlier_ = nh_.advertise<sensor_msgs::PointCloud2>("/outlier", 10);
    pub_diagnostics_ = nh_.advertise<diagnostic_msgs::DiagnosticArray>("/lidar_slam/diagnostics/" + ros::this_node::getName(), 10);

    timer_ = nh_.createTimer(ros::Duration(1.0), &IP::timerCB, this);
    sub_pc_ = nh_.subscribe<sensor_msgs::PointCloud2>("/lslidar_point_cloud", 10, boost::bind(&IP::pcCB, this, _1));

    if (debug_output) {
      ROS_INFO("ImageProjection onInit end: %.3f ms", t_init.toc());
    }
  }

  template <typename PointT>
  void removeClosedPointCloud(const pcl::PointCloud<PointT> &cloud_in,
                              pcl::PointCloud<PointT> &cloud_out, float thres)
  {
    if (&cloud_in != &cloud_out)
    {
      cloud_out.header = cloud_in.header;
      cloud_out.points.resize(cloud_in.points.size());
    }

    size_t j = 0;

    for (size_t i = 0; i < cloud_in.points.size(); ++i)
    {
      if (cloud_in.points[i].x * cloud_in.points[i].x + cloud_in.points[i].y * cloud_in.points[i].y + cloud_in.points[i].z * cloud_in.points[i].z < thres * thres)
        continue;
      cloud_out.points[j] = cloud_in.points[i];
      j++;
    }
    if (j != cloud_in.points.size())
    {
      cloud_out.points.resize(j);
    }

    cloud_out.height = 1;
    cloud_out.width = static_cast<uint32_t>(j);
    cloud_out.is_dense = true;
  }

  void pcCB(const sensor_msgs::PointCloud2ConstPtr &msg)
  {
    TicToc t_whole;

    seg_info_msg_->header = msg->header;
    // seg_info_msg_->header.stamp = ros::Time::now(); // Ouster lidar users may need to uncomment this line
    PointCloudT::Ptr cloud_in(new PointCloudT());
    pcl::fromROSMsg(*msg, *cloud_in);
    if (debug_output) {
      ROS_INFO_STREAM("cloud_in size: " << cloud_in->points.size());
    }
    
    // check if there is field intencity
    bool add_field = true;
    for (auto f = msg->fields.begin(); f != msg->fields.end(); f++)
      if (f->name == std::string("intensity"))
        add_field = false;
    if (add_field)
    {
      pcl::PointCloud<pcl::PointXYZI>::Ptr cloud_int(new pcl::PointCloud<pcl::PointXYZI>());
      pcl::copyPointCloud(*cloud_in, *cloud_int);
      cloud_in = cloud_int;
    }

    std::vector<int> indices;
    pcl::removeNaNFromPointCloud(*cloud_in, *cloud_in, indices);
    removeClosedPointCloud(*cloud_in, *cloud_in, 1.0); // remove closed points

    int cloud_size = cloud_in->points.size();
    seg_info_msg_->startOrientation = -atan2(cloud_in->points[0].y, cloud_in->points[0].x);
    seg_info_msg_->endOrientation = -atan2(cloud_in->points[cloud_size - 1].y, cloud_in->points[cloud_size - 1].x) + 2 * M_PI;
    if (seg_info_msg_->endOrientation - seg_info_msg_->startOrientation > 3 * M_PI)
    {
      seg_info_msg_->endOrientation -= 2 * M_PI;
    }
    else if (seg_info_msg_->endOrientation - seg_info_msg_->startOrientation < M_PI)
    {
      seg_info_msg_->endOrientation += 2 * M_PI;
    }
    seg_info_msg_->orientationDiff = seg_info_msg_->endOrientation - seg_info_msg_->startOrientation;

    double vertical_ang, horizon_ang;
    int row_id, col_id, index;
    lidar_params_validity_ = true;
    for (int i = 0; i < cloud_size; ++i)
    {
      auto &p = cloud_in->points[i];
      vertical_ang = RAD2ANGLE(atan2(p.z, hypot(p.x, p.y)));
      if ( LaserType::VLP_16 == laser_type || LaserType::VLP_32C == laser_type || LaserType::HDL_32E == laser_type || LaserType::HDL_64 == laser_type || LaserType::VLS_128 == laser_type || LaserType::OS1_16 == laser_type || LaserType::OS1_64 == laser_type || LaserType::LSLIDAR_C16 == laser_type )
      {
        row_id = (vertical_ang + ang_bottom) / ang_res_y + 0.5;
      }
      else if (LaserType::RFANS_16M == laser_type)
      {
        if (vertical_ang > 4.5)
        {
          row_id = 13 + (vertical_ang - 5.) / 3 + 0.5;
        }
        else if (vertical_ang > 0.5)
        {
          row_id = 11 + (vertical_ang - 1.0) / 2 + 0.5;
        }
        else if (vertical_ang > -7.)
        {
          row_id = 10.5 + vertical_ang;
        }
        else if (vertical_ang > -8.5)
        {
          row_id = 3;
        }
        else if (vertical_ang > -10.5)
        {
          row_id = 2;
        }
        else if (vertical_ang > -13.5)
        {
          row_id = 1;
        }
        else
        {
          row_id = 0;
        }
      }
      else
      {
        ROS_ERROR("not surpported laser type");
        return;
      }
      if (row_id < 0 || row_id >= N_SCAN)
      {
        ROS_WARN("error row_id");
        lidar_params_validity_ = false;
        continue;
      }

      horizon_ang = RAD2ANGLE(-atan2(p.y, p.x) + 2 * M_PI);
      col_id = horizon_ang / ang_res_x;
      if (col_id >= Horizon_SCAN)
      {
        col_id -= Horizon_SCAN;
      }
      if (col_id < 0 || col_id >= Horizon_SCAN)
      {
        ROS_WARN("error col_id %d ", col_id);
        lidar_params_validity_ = false;
        continue;
      }

      range_mat_(row_id, col_id) = sqrt(p.x * p.x + p.y * p.y + p.z * p.z);

      p.intensity = row_id + col_id / 10000.0;
      index = col_id + row_id * Horizon_SCAN;
      full_cloud_->points[index] = p;
    }

    // groundRemoval
    int lower_id, upper_id;
    double diff_x, diff_y, diff_z, angle;
    for (int j = 0; j < Horizon_SCAN; ++j)
    {
      for (int i = 0; i < ground_scan_id; ++i)
      {
        lower_id = j + i * Horizon_SCAN;
        upper_id = j + (i + 1) * Horizon_SCAN;

        if (-1 == full_cloud_->points[lower_id].intensity || -1 == full_cloud_->points[upper_id].intensity)
        {
          continue;
        }

        diff_x = full_cloud_->points[upper_id].x - full_cloud_->points[lower_id].x;
        diff_y = full_cloud_->points[upper_id].y - full_cloud_->points[lower_id].y;
        diff_z = full_cloud_->points[upper_id].z - full_cloud_->points[lower_id].z;
        angle = RAD2ANGLE(atan2(diff_z, hypot(diff_x, diff_y)));

        if (abs(angle - sensor_mount_ang) < 45.) // ground treshold
        {
          ground_mat_(i, j) = ground_mat_(i + 1, j) = true;
        }
      }
    }

    for (int i = 0; i < N_SCAN; ++i)
    {
      for (int j = 0; j < Horizon_SCAN; ++j)
      {
        if (ground_mat_(i, j) == true || range_mat_(i, j) == std::numeric_limits<double>::max())
        {
          label_mat_(i, j) = -1;
        }
      }
    }

    // cloudSegmentation
    // ????????????????????????????????????????????? outlier / Simple clustering of non-ground points, in order to find outlier
    for (int i = 0; i < N_SCAN; ++i)
    {
      for (int j = 0; j < Horizon_SCAN; ++j)
      {
        if (label_mat_(i, j) == 0)
        {
          labelComponents(i, j);
        }
      }
    }

    int line_size = 0;
    for (int i = 0; i < N_SCAN; ++i)
    {
      seg_info_msg_->startRingIndex[i] = line_size + 5;
      for (int j = 0; j < Horizon_SCAN; ++j)
      {
        if (label_mat_(i, j) > 0 || ground_mat_(i, j) == true)
        {
          // TODO: ???????????????????????????????????????????????????????????? / Does the filtering of noise and ground points improve the result here?
          if (label_mat_(i, j) == 999999)
          {
            if (i > ground_scan_id && j % 5 == 0)
            {
              outlier_cloud_->points.push_back(full_cloud_->points[j + i * Horizon_SCAN]);
            }
            continue;
          }
          else if (ground_mat_(i, j) == true)
          {
            if (j % 5 != 0 && j > 4 && j < Horizon_SCAN - 5)
            {
              continue;
            }
          }

          seg_info_msg_->segmentedCloudGroundFlag[line_size] = (ground_mat_(i, j) == true);
          seg_info_msg_->segmentedCloudColInd[line_size] = j;
          seg_info_msg_->segmentedCloudRange[line_size] = range_mat_(i, j);
          segmented_cloud_->points.push_back(full_cloud_->points[j + i * Horizon_SCAN]);
          ++line_size;
        }
      }
      seg_info_msg_->endRingIndex[i] = line_size - 1 - 5;
    }
    publish();
    /* if (seg_info_msg_->orientationDiff < 6 || seg_info_msg_->orientationDiff > 6.5 )
    {
      ROS_WARN_STREAM("not complete point cloud received! orientationDiff: " << seg_info_msg_->orientationDiff ); // ????-???? ????????, ?????? ???????????????? ???? ???????????? ???????????? ?????????? (????????????????, ???????????? ?? 40 ???????????????? ???????????? 360 ????????????????) ?????????????????? ???????????????? ?????????? ???????? ??????????????????????
    }
    else
    {
      publish();
    }
    if (debug_output) {
      ROS_INFO_STREAM("segmented_cloud size: " << segmented_cloud_->points.size());
      ROS_INFO_STREAM("outlier_cloud size: " << outlier_cloud_->points.size());
    } */

    segmented_cloud_->clear();
    outlier_cloud_->clear();
    range_mat_.setConstant(std::numeric_limits<double>::max());
    label_mat_.setZero();
    ground_mat_.setZero();
    label_cnt_ = 1;
    PointT nan_p;
    nan_p.intensity = -1;
    std::fill(full_cloud_->points.begin(), full_cloud_->points.end(), nan_p);

    last_cloud_msg_stamp_ = msg->header.stamp;

    if (debug_output) {
      ROS_INFO("image projection time: : %.3f ms", t_whole.toc());
    }
  }

  void timerCB(const ros::TimerEvent& e){
    diagnostic_msg_.header.stamp = ros::Time::now();

    if (lidar_params_validity_) 
    {
        diagnostic_msg_.status[0].level = diagnostic_msgs::DiagnosticStatus::OK;
        diagnostic_msg_.status[0].message = "Correct";
    }
    else
    {
        diagnostic_msg_.status[0].level = diagnostic_msgs::DiagnosticStatus::WARN;
        diagnostic_msg_.status[0].message = "Incorrect projection parameters. Check LiDAR type or projection parameters!";
    }

    double cloud_diff = abs(e.current_real.toSec() - last_cloud_msg_stamp_.toSec());
    if (cloud_diff > 1.0)
    {
        diagnostic_msg_.status[1].level = diagnostic_msgs::DiagnosticStatus::STALE;
        diagnostic_msg_.status[1].message = "No pointcloud input data for " + std::to_string(cloud_diff) + "s";
    }
    else 
    {
        diagnostic_msg_.status[1].level = diagnostic_msgs::DiagnosticStatus::OK;
        diagnostic_msg_.status[1].message = "Ok";
    }
    diagnostic_msg_.status[1].values[0].value = std::to_string(last_cloud_msg_stamp_.toSec());

    pub_diagnostics_.publish(diagnostic_msg_);    
  }

  void labelComponents(int row, int col)
  {
    double d1, d2, alpha, angle;
    int from_id_i, from_id_j, this_id_i, this_id_j;
    bool line_cnt_flag[N_SCAN] = {false};

    queue<int> que_id_i, que_id_j;
    queue<int> all_pushed_id_i, all_pushed_id_j;
    que_id_i.push(row);
    que_id_j.push(col);
    line_cnt_flag[row] = true; // ??? LeGO_LOAM ??????????????? / Not in the original LeGO_LOAM source code
    all_pushed_id_i.push(row);
    all_pushed_id_j.push(col);

    while (!que_id_i.empty())
    {
      from_id_i = que_id_i.front();
      from_id_j = que_id_j.front();
      que_id_i.pop();
      que_id_j.pop();
      label_mat_(from_id_i, from_id_j) = label_cnt_;
      line_cnt_flag[from_id_i] = true;

      for (const auto &iter : neighbor_iter_)
      {
        this_id_i = from_id_i + iter.first;
        this_id_j = from_id_j + iter.second;
        if (this_id_i < 0 || this_id_i >= N_SCAN)
        {
          continue;
        }
        if (this_id_j < 0)
        {
          this_id_j = Horizon_SCAN - 1;
        }
        else if (this_id_j >= Horizon_SCAN)
        {
          this_id_j = 0;
        }
        if (label_mat_(this_id_i, this_id_j))
        {
          // ??????????????????????????????/Nan ??? /  Visited, or ground / Nan point
          continue;
        }

        d1 = max(range_mat_(from_id_i, from_id_j), range_mat_(this_id_i, this_id_j));
        d2 = min(range_mat_(from_id_i, from_id_j), range_mat_(this_id_i, this_id_j));

        if (iter.first == 0)
        {
          alpha = seg_alpha_x;
        }
        else
        {
          alpha = seg_alpha_y;
        }

        // ??? seg_alpha_y ??????????????????????????????????????????????????? / It is difficult to cluster at far points on seg_alpha_y, it is estimated to be good at close
        // ??? seg_alpha_x ????????????????????? / Clustering on seg_alpha_x is acceptable
        angle = atan2(d2 * sin(alpha), (d1 - d2 * cos(alpha)));
        if (angle > seg_theta)
        {
          que_id_i.push(this_id_i);
          que_id_j.push(this_id_j);
          label_mat_(this_id_i, this_id_j) = label_cnt_;
          line_cnt_flag[this_id_i] = true;
          all_pushed_id_i.push(this_id_i);
          all_pushed_id_j.push(this_id_j);
        }
      }
    }

    bool feasible_seg = false;
    if (all_pushed_id_i.size() >= 30) //min_segm_point_num
    {
      feasible_seg = true;
    }
    else if (all_pushed_id_i.size() >= seg_valid_point_num)
    {
      int line_cnt = 0;
      for (int i = 0; i < N_SCAN; ++i)
      {
        if (line_cnt_flag[i])
        {
          ++line_cnt;
        }
      }
      if (line_cnt >= seg_valid_line_num)
      {
        feasible_seg = true;
      }
    }

    if (feasible_seg)
    {
      ++label_cnt_;
    }
    else
    {
      while (!all_pushed_id_i.empty())
      {
        label_mat_(all_pushed_id_i.front(), all_pushed_id_j.front()) = 999999;
        all_pushed_id_i.pop();
        all_pushed_id_j.pop();
      }
    }
  }

  void publish()
  {
    if (pub_seg_info_.getNumSubscribers() > 0)
    {
      pub_seg_info_.publish(seg_info_msg_);
    }
    if (pub_segmented_cloud_.getNumSubscribers() > 0)
    {
      pcl::toROSMsg(*segmented_cloud_, *segmented_cloud_msg_);
      segmented_cloud_msg_->header = seg_info_msg_->header;
      pub_segmented_cloud_.publish(segmented_cloud_msg_);
    }
    if (pub_outlier_.getNumSubscribers() > 0)
    {
      pcl::toROSMsg(*outlier_cloud_, *outlier_cloud_msg_);
      outlier_cloud_msg_->header = seg_info_msg_->header;
      pub_outlier_.publish(outlier_cloud_msg_);
    }
  }

}; // namespace loam

int main(int argc, char **argv)
{
  ros::init(argc, argv, "IP");
  ros::NodeHandle nh;
  IP ip(nh);
  ros::spin();
  return 0;
}
